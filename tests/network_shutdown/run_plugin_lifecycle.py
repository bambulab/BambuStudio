#!/usr/bin/env python3
"""Run the real locally installed SDK under macOS Guard Malloc.

Usage: run_plugin_lifecycle.py PLUGIN_DYLIB STUDIO_CERT_DIRECTORY [--runs N]
Only fresh temporary configuration is used. No credentials are copied.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('plugin', type=Path)
p.add_argument('certificates', type=Path)
p.add_argument('--runs', type=int, default=3)
a = p.parse_args()
if platform.system() != 'Darwin' or a.runs < 1:
    p.error('macOS and a positive run count are required')
plugin = a.plugin.resolve(strict=True)
certificates = a.certificates.resolve(strict=True)
if not (certificates / 'slicer_base64.cer').is_file():
    p.error('Studio certificate file is missing')
with tempfile.TemporaryDirectory(prefix='studio-discovery-regression-') as directory:
    root = Path(directory)
    executable = root / 'probe'
    subprocess.run(['clang++', '-std=c++17', '-O0', '-g', str(Path(__file__).with_name('plugin_lifecycle.cpp')), '-o', str(executable)], check=True)
    results = []
    env = dict(os.environ, DYLD_INSERT_LIBRARIES='/usr/lib/libgmalloc.dylib')
    for mode in ['baseline', 'stop', 'stop-immediate']:
        for index in range(a.runs):
            state = root / f'{mode}-{index}'
            state.mkdir()
            try:
                r = subprocess.run([str(executable), str(plugin), str(state), str(certificates), mode], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=45)
                # An initialization failure or unrelated pre-destroy fault does not count.
                passed = (r.returncode in (-signal.SIGSEGV, -signal.SIGBUS) and 'destroy_returned' in r.stdout) if mode == 'baseline' else (r.returncode == 0 and 'post_destroy_wait_passed' in r.stdout and 'discovery_stopped' in r.stdout)
                result = dict(mode=mode, run=index+1, returncode=r.returncode, passed=passed)
            except subprocess.TimeoutExpired:
                result = dict(mode=mode, run=index+1, passed=False, timeout=True)
            results.append(result)
            print(json.dumps(result), flush=True)
    if not all(r['passed'] for r in results):
        raise SystemExit(1)
