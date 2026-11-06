#!/usr/bin/env python3
"""Launch a built Mac app with a fresh profile and check its own native window.

Uses CoreGraphics window metadata, without requesting Accessibility or screen
recording permission. This checks startup, not interactive updater/save dialogs.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('results', type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Run this test on macOS')
    executable = (args.bundle / 'Contents/MacOS/BambuStudio').resolve(strict=True)
    results = args.results.resolve()
    results.mkdir(parents=True, exist_ok=False)
    profile = results / 'profile'
    profile.mkdir()
    (profile / 'BambuStudio.conf').write_text(json.dumps({
        'app': {'language': 'en_US', 'region': 'North America'},
        'firstguide': {'finish': True, 'privacyuse': False},
        'models': [{'model': 'Bambu Lab A1', 'vendor': 'BBL', 'nozzle_diameter': '0.4'}],
    }))
    probe = results / 'windows.swift'
    probe.write_text('''import Foundation
import CoreGraphics
let pid = Int(CommandLine.arguments[1])!
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
let own = windows.filter { ($0[kCGWindowOwnerPID as String] as? Int) == pid }
let data = try! JSONSerialization.data(withJSONObject: own, options: [.prettyPrinted, .sortedKeys])
print(String(data: data, encoding: .utf8)!)
let large = own.contains { entry in
    let bounds = entry[kCGWindowBounds as String] as? [String: Any] ?? [:]
    return (entry[kCGWindowLayer as String] as? Int) == 0 &&
           (bounds["Width"] as? Double ?? 0) > 700 && (bounds["Height"] as? Double ?? 0) > 400
}
exit(large ? 0 : 1)
''')
    subprocess.run(['swiftc', str(probe), '-o', str(results / 'windows')], check=True, timeout=60)
    with (results / 'startup.log').open('w') as log:
        process = subprocess.Popen([str(executable), '--datadir', str(profile)],
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                time.sleep(2)
                if process.poll() is not None:
                    raise RuntimeError(f'App exited with {process.returncode}; see {results}/startup.log')
                windows = subprocess.run([str(results / 'windows'), str(process.pid)],
                                         text=True, capture_output=True, timeout=10)
                (results / 'windows.json').write_text(windows.stdout)
                if windows.returncode == 0:
                    time.sleep(10)
                    if process.poll() is not None:
                        raise RuntimeError('App exited after creating its main window')
                    print('PASS native macOS full-size onscreen window with isolated profile')
                    return
            raise RuntimeError('No full-size onscreen application window')
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()


if __name__ == '__main__':
    main()
