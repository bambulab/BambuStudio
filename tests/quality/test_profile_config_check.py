import importlib.util
import tempfile
import unittest
from pathlib import Path


WORKTREE_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = WORKTREE_ROOT / "scripts" / "quality" / "profile_config_check.py"


def load_module():
    if not SCRIPT_PATH.exists():
        raise AssertionError(f"missing validator script: {SCRIPT_PATH}")
    spec = importlib.util.spec_from_file_location("profile_config_check", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ProfileConfigCheckTests(unittest.TestCase):
    def write_file(self, root: Path, relative_path: str, content: str) -> Path:
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def test_valid_assets_pass_validation(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            json_file = self.write_file(
                root,
                "profiles/valid_profile.json",
                '{\n'
                '  "name": "Demo PLA",\n'
                '  "type": "filament",\n'
                '  "from": "system",\n'
                '  "setting_id": "demo_pla",\n'
                '  "compatible_printers": ["Bambu Lab A1"],\n'
                '  "nozzle_diameter": ["0.4"]\n'
                '}\n',
            )
            ini_file = self.write_file(root, "data/hints.ini", "filament_colour = #ABCD\n")

            errors = module.validate_paths([json_file, ini_file])

            self.assertEqual(errors, [])

    def test_invalid_json_reports_error(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            broken_file = self.write_file(root, "profiles/broken.json", '{"name": "oops"\n')

            errors = module.validate_paths([broken_file])

            self.assertEqual(len(errors), 1)
            self.assertIn("invalid JSON", errors[0])
            self.assertIn("broken.json", errors[0])

    def test_empty_required_string_field_is_rejected(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            broken_file = self.write_file(
                root,
                "profiles/empty_name.json",
                '{\n  "name": "   ",\n  "type": "filament"\n}\n',
            )

            errors = module.validate_paths([broken_file])

            self.assertEqual(len(errors), 1)
            self.assertIn("name must be a non-empty string", errors[0])

    def test_empty_string_inside_string_list_is_rejected(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            broken_file = self.write_file(
                root,
                "profiles/empty_list_member.json",
                '{\n'
                '  "name": "Demo PLA",\n'
                '  "type": "filament",\n'
                '  "compatible_printers": ["Bambu Lab A1", "  "]\n'
                '}\n',
            )

            errors = module.validate_paths([broken_file])

            self.assertEqual(len(errors), 1)
            self.assertIn("compatible_printers[1] must be a non-empty string", errors[0])

    def test_invalid_ini_reports_error(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            broken_file = self.write_file(root, "data/hints.ini", "[broken\nvalue = 1\n")

            errors = module.validate_paths([broken_file])

            self.assertEqual(len(errors), 1)
            self.assertIn("invalid INI", errors[0])
            self.assertIn("hints.ini", errors[0])

    def test_main_returns_non_zero_when_errors_exist(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_file(root, "profiles/broken.json", '{"name": ""}\n')

            exit_code = module.main([str(root)])

            self.assertEqual(exit_code, 1)


if __name__ == "__main__":
    unittest.main()
