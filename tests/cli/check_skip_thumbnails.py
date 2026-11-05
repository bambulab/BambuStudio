#!/usr/bin/env python3
"""Integration check: built BambuStudio binary + a known-valid, deterministic project fixture.

Usage: python3 check_skip_thumbnails.py /path/to/BambuStudio fixture.3mf [plate]
Run in a headless/sandboxed environment. The fixture is copied, never rewritten.
Use make_headless_fixture.py: complex projects may reorder islands even between
two flag-free baselines, making exact G-code equality an unsuitable oracle.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET


def executable_lines(path):
    return [line.split(';', 1)[0].strip() for line in path.read_text().splitlines()
            if line.split(';', 1)[0].strip()]


def main():
    binary, fixture = (Path(p).resolve(strict=True) for p in sys.argv[1:3])
    plate = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    before = hashlib.sha256(fixture.read_bytes()).digest()
    with tempfile.TemporaryDirectory(prefix='bbs-no-thumbnails-') as name:
        root = Path(name)
        project = root / 'fixture-without-cached-images.3mf'
        # Force the export to need thumbnails, even when the supplied fixture
        # normally reuses images and would not exercise offscreen initialization.
        with zipfile.ZipFile(fixture) as source, zipfile.ZipFile(project, 'w') as target:
            for item in source.infolist():
                if not item.filename.lower().endswith(('.png', '.jpg', '.jpeg', '.webp')):
                    target.writestr(item, source.read(item))
        state, output = root / 'state', root / 'output'
        state.mkdir()
        output.mkdir()
        command = [str(binary), '--datadir', str(state), '--skip-thumbnails',
                   '--debug', '3', '--slice', str(plate), '--export-3mf', 'result.3mf',
                   '--outputdir', str(output), str(project)]
        run = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=300)
        assert run.returncode == 0, run.stdout + run.stderr
        result = json.loads((output / 'result.json').read_text())
        assert result['return_code'] == 0, result
        assert (output / f'plate_{plate}.gcode').stat().st_size > 0
        with zipfile.ZipFile(output / 'result.3mf') as archive:
            assert archive.testzip() is None
            assert '3D/3dmodel.model' in archive.namelist()
            assert f'Metadata/plate_{plate}.gcode' in archive.namelist()
            assert not any(n.lower().endswith('.png') for n in archive.namelist())
            relationships = ET.fromstring(archive.read('_rels/.rels'))
            for relation in relationships:
                if relation.get('TargetMode') != 'External':
                    assert relation.attrib['Target'].lstrip('/') in archive.namelist(), relation.attrib
            bounds = json.loads(archive.read(f'Metadata/plate_{plate}.json'))
            assert bounds['bbox_all'], bounds
        log = run.stdout + run.stderr
        assert 'glfwInit Success' not in log and 'opengl version' not in log, log

        baseline = root / 'baseline'
        baseline.mkdir()
        control = subprocess.run([str(binary), '--datadir', str(state), '--slice', str(plate),
                                  '--outputdir', str(baseline), str(project)],
                                 cwd=root, capture_output=True, text=True, timeout=300)
        assert control.returncode == 0, control.stdout + control.stderr
        assert executable_lines(output / f'plate_{plate}.gcode') == executable_lines(baseline / f'plate_{plate}.gcode')

        # A non-Bambu identity exercises the second rendering entry point: G-code
        # export embeds thumbnails there even when --export-3mf is not requested.
        generic_project = root / 'generic-printer.3mf'
        with zipfile.ZipFile(project) as source:
            files = {name: source.read(name) for name in source.namelist()}
        config = json.loads(files['Metadata/project_settings.config'])
        config['printer_model'] = 'Regression printer'
        config['printer_settings_id'] = 'Regression printer 0.4 nozzle'
        files['Metadata/project_settings.config'] = json.dumps(config).encode()
        with zipfile.ZipFile(generic_project, 'w', zipfile.ZIP_DEFLATED) as target:
            for filename, data in files.items():
                target.writestr(filename, data)
        generic_output = root / 'generic-output'
        generic_output.mkdir()
        generic = subprocess.run([str(binary), '--datadir', str(state), '--skip-thumbnails',
                                  '--debug', '3', '--slice', str(plate), '--outputdir',
                                  str(generic_output), str(generic_project)], cwd=root,
                                 capture_output=True, text=True, timeout=300)
        assert generic.returncode == 0, generic.stdout + generic.stderr
        assert (generic_output / f'plate_{plate}.gcode').stat().st_size > 0
        assert 'opengl version' not in generic.stdout + generic.stderr

        invalid = root / 'invalid'
        invalid.mkdir()
        bad = subprocess.run([str(binary), '--datadir', str(state), '--skip-thumbnails',
                              '--export-png', str(plate), '--outputdir', str(invalid),
                              str(project)], cwd=root, capture_output=True, text=True, timeout=30)
        assert bad.returncode != 0
        assert json.loads((invalid / 'result.json').read_text())['return_code'] == -2
        assert '--skip-thumbnails cannot be combined with --export-png' in bad.stdout + bad.stderr
    assert hashlib.sha256(fixture.read_bytes()).digest() == before
    print('PASS: headless Bambu/generic slice, unchanged executable G-code, no image generation, invalid PNG combination, input unchanged')


if __name__ == '__main__':
    main()
