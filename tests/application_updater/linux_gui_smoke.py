#!/usr/bin/env python3
"""Run an AppImage in an isolated X11 profile; requires Xvfb, xdotool and PyGObject.

This checks main-window startup only. It does not assert updater integration.
The results directory must be new so an existing user profile is never reused.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def capture(results, name):
    tree = subprocess.check_output(['xwininfo', '-root', '-tree'], text=True)
    (results / (name + '-windows.txt')).write_text(tree)
    import gi
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk
    root = Gdk.get_default_root_window()
    Gdk.pixbuf_get_from_window(root, 0, 0, root.get_width(), root.get_height()).savev(
        str(results / (name + '.png')), 'png', [], [])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appimage', type=Path)
    parser.add_argument('results', type=Path)
    args = parser.parse_args()
    image = args.appimage.resolve(strict=True)
    results = args.results.resolve()
    results.mkdir(parents=True, exist_ok=False)
    profile = results / 'profile'
    profile.mkdir()
    # Avoid the first-run language prompt on minimal distro images. The target
    # must provide en_US.UTF-8; install/generate it before running this check.
    (profile / 'BambuStudio.conf').write_text(json.dumps({
        'app': {'language': 'en_US', 'region': 'North America'},
        'firstguide': {'finish': True, 'privacyuse': False},
        'models': [{'model': 'Bambu Lab A1', 'vendor': 'BBL', 'nozzle_diameter': '0.4'}],
    }))
    env = os.environ.copy()
    env.update(XDG_CONFIG_HOME=str(results / 'config'),
               XDG_DATA_HOME=str(results / 'data'),
               XDG_CACHE_HOME=str(results / 'cache'),
               LIBGL_ALWAYS_SOFTWARE='1', APPIMAGE_EXTRACT_AND_RUN='1',
               LANG='en_US.UTF-8', LC_ALL='en_US.UTF-8')
    for ca in ('/etc/ssl/certs/ca-certificates.crt',
               '/etc/pki/tls/certs/ca-bundle.crt', '/etc/ssl/ca-bundle.pem'):
        if Path(ca).is_file():
            env['SSL_CERT_FILE'] = ca
            break
    with (results / 'startup.log').open('w') as log:
        process = subprocess.Popen([str(image), '--datadir', str(profile)],
                                   env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            deadline = time.monotonic() + 90
            main_window = ''
            while time.monotonic() < deadline:
                time.sleep(1)
                if process.poll() is not None:
                    raise RuntimeError(f'App exited with {process.returncode}; see {results}/startup.log')
                found = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', '^Untitled$'],
                                       text=True, capture_output=True)
                if found.returncode == 0:
                    main_window = found.stdout.splitlines()[0]
                    break
            if not main_window:
                raise RuntimeError('No visible Untitled main window within 90 seconds')
            time.sleep(10)
            if process.poll() is not None:
                raise RuntimeError('App exited after creating its main window')
            capture(results, 'startup')
            output = (results / 'startup.log').read_text(errors='replace')
            if 'missing the corresponding locale' in output or 'Switching BambuStudio to language' in output:
                raise RuntimeError('Locale initialization failed')
            print(f'PASS main-window startup: window {main_window}; inspect {results}/startup.png for modal prompts')
        finally:
            # Preserve evidence even when startup fails.
            try:
                capture(results, 'final')
            except Exception as error:
                print(f'Could not capture final screenshot: {error}')
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()


if __name__ == '__main__':
    main()
