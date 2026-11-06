#!/usr/bin/env python3
"""Native DMG/signature/LaunchServices checks; never touches installed user apps."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import time


def run(*args, ok=True, **kwargs):
    result = subprocess.run([str(x) for x in args], text=True, capture_output=True, **kwargs)
    if ok and result.returncode:
        raise RuntimeError(f'{args}: {result.stdout}\n{result.stderr}')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('driver', type=Path)
    options = parser.parse_args()
    driver = options.driver.resolve()
    repository = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="bambu native ' $ é ") as temporary:
        root = Path(temporary)
        marker = root / 'launched.txt'
        profile = root / "profile [saved] ' $ é 漢"
        profile.mkdir()
        source = root / 'fixture.c'
        source.write_text('#include <stdio.h>\nint main(int argc, char **argv) { FILE *f = fopen(' + json.dumps(str(marker)) + ', "w"); if (!f) return 2; fputs("launched", f); for (int i=1; i<argc; ++i) { fputc(10, f); fputs(argv[i], f); } return fclose(f); }\n')
        seed = root / 'seed/BambuStudio.app'
        (seed / 'Contents/MacOS').mkdir(parents=True)
        (seed / 'Contents/Resources').mkdir()
        with (seed / 'Contents/Info.plist').open('wb') as stream:
            plistlib.dump({'CFBundleIdentifier': 'test.bambu.updater.' + str(os.getpid()),
                          'CFBundleName': 'Bambu updater fixture', 'CFBundleExecutable': 'BambuStudio',
                          'CFBundlePackageType': 'APPL', 'CFBundleVersion': '1', 'LSUIElement': True}, stream)
        run('clang', '-arch', 'arm64', '-arch', 'x86_64', source, '-o', seed / 'Contents/MacOS/BambuStudio')
        (seed / 'Contents/Resources/version').write_text('new')
        run('codesign', '--force', '--sign', '-', seed)
        image = root / 'update.dmg'
        run('bash', repository / 'packaging/updates/macos-dmg.sh', seed, image)
        helper = run(driver, 'mac-script').stdout
        # Prevent failure tests from displaying modal alerts in the user's desktop.
        # All native filesystem, signing, mounting and launch commands are retained.
        helper = helper.replace('/usr/bin/osascript', '/usr/bin/true')
        assertions = 0

        def check(condition, message):
            nonlocal assertions
            assertions += 1
            if not condition:
                raise AssertionError(message)

        def installation(name):
            app = root / name / "Bambu [Studio] ' $ é.app"
            app.parent.mkdir()
            shutil.copytree(seed, app)
            (app / 'Contents/Resources/version').write_text('old')
            return app

        def stage(app, package=image):
            directory = app.parent / 'stage'
            directory.mkdir()
            shutil.copy2(package, directory / 'update.dmg')
            return directory

        for scenario in ('success', 'changed-install', 'unsafe-backup', 'rollback'):
            app = installation(scenario)
            backup = Path(str(app) + '.previous')
            shutil.copytree(app, backup)
            (backup / 'Contents/Resources/version').write_text('older')
            directory = stage(app)
            run(driver, 'prepare-mac', directory / 'update.dmg', app)
            check((directory / 'new.app/Contents/Resources/version').read_text() == 'new', 'DMG app was not prepared')
            check(not os.path.ismount(directory / 'mount'), 'DMG remained mounted')
            active_helper = helper
            if scenario == 'changed-install':
                (directory / 'identity').write_text('changed')
            elif scenario == 'unsafe-backup':
                shutil.rmtree(backup)
                backup.symlink_to(app, target_is_directory=True)
            elif scenario == 'rollback':
                # Fault injection at the second rename exercises the native rollback.
                active_helper = helper.replace('/bin/mv "$stage/new.app" "$app"', '/usr/bin/false')
            result = run('/bin/sh', '-c', active_helper, 'bambu-native-test', '999999999', app, directory, profile, ok=False)
            expected = scenario == 'success'
            check((result.returncode == 0) == expected, f'{scenario}: {result.stderr}')
            check((app / 'Contents/Resources/version').read_text() == ('new' if expected else 'old'), scenario + ': installation changed incorrectly')
            check(not Path(str(app) + '.update-lock').exists(), 'Replacement lock leaked')
            if expected:
                check((backup / 'Contents/Resources/version').read_text() == 'old', 'Previous app missing')
                check(not directory.exists(), 'Staging directory leaked')
                for _ in range(100):
                    if marker.exists():
                        break
                    time.sleep(.1)
                check(marker.read_text().splitlines()[0] == 'launched', 'LaunchServices did not launch replacement')
                check(marker.read_text().splitlines()[1:] == ['--datadir', str(profile)], 'LaunchServices lost or changed the custom data directory')
            elif scenario == 'rollback':
                check((backup / 'Contents/Resources/version').read_text() == 'older', 'Older backup not restored')
            print('PASS native macOS ' + scenario)

        # A modified signed resource must fail native codesign verification.
        tampered = root / 'tampered/BambuStudio.app'
        shutil.copytree(seed, tampered)
        (tampered / 'Contents/Resources/version').write_text('tampered')
        bad_image = root / 'tampered.dmg'
        run('hdiutil', 'create', '-srcfolder', tampered.parent, '-format', 'UDZO', bad_image)
        app = installation('bad-signature')
        directory = stage(app, bad_image)
        result = run(driver, 'prepare-mac', directory / 'update.dmg', app, ok=False)
        check(result.returncode != 0, 'Invalid signature accepted')
        check(not (directory / 'new.app').exists(), 'Invalid bundle copied')
        check(not os.path.ismount(directory / 'mount'), 'Failed signature left mounted image')
        check((app / 'Contents/Resources/version').read_text() == 'old', 'Bad signature changed installation')
        print('PASS native macOS signature rejection')

        # A valid Intel-only bundle cannot update a native Apple Silicon app.
        intel = root / 'intel/BambuStudio.app'
        shutil.copytree(seed, intel)
        run('clang', '-arch', 'x86_64', source, '-o', intel / 'Contents/MacOS/BambuStudio')
        run('codesign', '--force', '--sign', '-', intel)
        intel_image = root / 'intel.dmg'
        run('hdiutil', 'create', '-srcfolder', intel.parent, '-format', 'UDZO', intel_image)
        app = installation('wrong-architecture')
        directory = stage(app, intel_image)
        result = run(driver, 'prepare-mac', directory / 'update.dmg', app, ok=False)
        check(result.returncode != 0, 'Incompatible architecture accepted')
        check(not os.path.ismount(directory / 'mount'), 'Architecture failure left mounted image')
        print('PASS native macOS architecture rejection')

        mount = root / 'readonly-mount'
        mount.mkdir()
        run('hdiutil', 'attach', '-readonly', '-nobrowse', '-noautoopen', '-mountpoint', mount, image)
        try:
            app = mount / 'BambuStudio.app'
            result = run(driver, 'prepare-mac', image, app, ok=False)
            check(result.returncode != 0, 'Read-only mounted installation accepted')
        finally:
            run('hdiutil', 'detach', mount)
        print('PASS native macOS read-only installation rejection')
        print(f'PASS {assertions} native macOS assertions (real DMGs, codesign, lipo, ditto, stat, renames and LaunchServices)')


if __name__ == '__main__':
    main()
