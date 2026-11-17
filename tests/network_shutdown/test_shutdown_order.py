#!/usr/bin/env python3
"""SDK-free source contract checks for GUI_App::OnExit, not runtime SDK tests."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import unittest

SOURCE = Path(__file__).resolve().parents[2] / 'src/slic3r/GUI/GUI_App.cpp'


def on_exit():
    source = SOURCE.read_text(encoding='utf-8')
    # OnExit is a top-level definition; its closing brace starts at column zero.
    match = re.search(r'^int GUI_App::OnExit\(\)\s*\{.*?^\}', source, re.M | re.S)
    if not match:
        raise AssertionError('GUI_App::OnExit definition not found')
    return match.group()


class ShutdownOrderTest(unittest.TestCase):
    def test_platform_shutdown_contract(self):
        # Preprocess the actual body, so an accidental platform-only discovery
        # stop cannot pass by merely appearing in the source text.
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        for platform in ('linux', 'macos', 'windows'):
            for public in (0, 1):
                with self.subTest(platform=platform, public=public):
                    flags = ['-U__APPLE__', '-U__WINDOWS__', '-U_WIN32', '-U__linux__',
                             f'-DBBL_RELEASE_TO_PUBLIC={public}']
                    flags += {'linux': ['-D__linux__'], 'macos': ['-D__APPLE__'],
                              'windows': ['-D__WINDOWS__', '-D_WIN32']}[platform]
                    body = subprocess.run(compiler + ['-E', '-P', '-x', 'c++', *flags, '-'],
                                          input=on_exit(), text=True, capture_output=True,
                                          check=True).stdout
                    self.assert_order(body, [
                        'set_closing(true);',
                        'Slic3r::HelioQuery::shutdown_background_requests();',
                        'stop_sync_user_preset();',
                        'm_check_cert_thread.join();',
                        'm_minors_decisions_thread.join();',
                        'm_agent->start_discovery(false, false);',
                    ])
                    self.assertRegex(body, r'OnExit\(\)\s*\{\s*set_closing\(true\);')
                    for worker in ('m_check_cert_thread', 'm_minors_decisions_thread'):
                        self.assertRegex(body, rf'if\s*\({worker}\.joinable\(\)\)\s*{worker}\.join\(\);')
                    stop = 'm_agent->start_discovery(false, false);'
                    self.assertEqual(body.count('start_discovery('), 1)
                    self.assertRegex(body, r'if\s*\(m_agent\)\s*m_agent->start_discovery\(false, false\);')
                    for consumer in ('m_fila_manager_cloud_disp', 'm_fila_manager_cloud_sync',
                                     'm_fila_manager_cloud_client', 'm_fila_manager_sync',
                                     'm_fila_manager_store', 'm_device_manager', 'm_user_manager'):
                        self.assert_order(body, [stop, f'delete {consumer};', 'delete m_agent;'])
                    self.assert_order(body, ['delete m_agent;', 'app_config->save();', 'return wxApp::OnExit();'])
                    if platform == 'macos':
                        self.assert_order(body, ['set_closing(true);', 'UnRegisterMacPowerCallBack();',
                                                 'stop_sync_user_preset();'])

    def assert_order(self, body, statements):
        positions = [body.index(statement) for statement in statements]
        self.assertEqual(positions, sorted(positions), f'Incorrect shutdown order: {statements}')


if __name__ == '__main__':
    unittest.main()
