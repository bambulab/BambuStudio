#!/usr/bin/env python3
"""Create a synthetic single-nozzle cube using the executable's own defaults."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

VERTICES = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0),
            (0, 0, 10), (10, 0, 10), (10, 10, 10), (0, 10, 10)]
FACES = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
         (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]


def make_fixture(binary: Path, destination: Path, allow_rendering=False):
    with tempfile.TemporaryDirectory(prefix='bbs-fixture-defaults-') as directory:
        root = Path(directory)
        state = root / 'state'
        state.mkdir()
        stl = ['solid cube']
        for face in FACES:
            stl += ['facet normal 0 0 0', 'outer loop']
            stl += ['vertex %s %s %s' % VERTICES[i] for i in face]
            stl += ['endloop', 'endfacet']
        stl += ['endsolid cube']
        model_path = root / 'cube.stl'
        model_path.write_text('\n'.join(stl))
        defaults = root / 'defaults.json'
        result = subprocess.run([str(binary), '--datadir', str(state), '--export-settings',
                                 str(defaults), str(model_path)], cwd=root,
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        config = json.loads(defaults.read_text())
        base_project = root / 'base.3mf'
        command = [str(binary), '--datadir', str(state), '--export-3mf', str(base_project), str(model_path)]
        if not allow_rendering:
            command.insert(3, '--skip-thumbnails')
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        with zipfile.ZipFile(base_project) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}

    # Synthetic names and a Bambu-style single-nozzle identity select the direct
    # G-code export path. This is a software regression fixture, not a print profile.
    config.update({
        'printer_model': 'Bambu Lab P1S',
        'printer_settings_id': 'Bambu Lab P1S 0.4 nozzle',
        'print_settings_id': 'Headless regression 0.20mm',
        'filament_settings_id': ['Headless regression PLA'],
        'filament_ids': ['GFL99'], 'filament_type': ['PLA'],
        'filament_colour': ['#FFFFFF'], 'filament_diameter': ['1.75'],
        'filament_density': ['1.24'], 'filament_self_index': ['1'],
        'filament_map': ['1'], 'filament_volume_map': ['0'],
        'inherits_group': ['', '', ''], 'nozzle_diameter': ['0.4'],
        'printable_area': ['0x0', '256x0', '256x256', '0x256'],
        'printable_height': '256', 'layer_height': '0.2',
        'initial_layer_print_height': '0.2', 'wall_loops': '2',
        'sparse_infill_density': '15%', 'enable_support': '0',
        'enable_prime_tower': '0',
    })
    files['Metadata/project_settings.config'] = json.dumps(config).encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--allow-rendering-for-fixture', action='store_true',
                        help='Only for an older executable, run outside the process sandbox')
    args = parser.parse_args()
    make_fixture(args.binary.resolve(strict=True), args.destination.resolve(), args.allow_rendering_for_fixture)
