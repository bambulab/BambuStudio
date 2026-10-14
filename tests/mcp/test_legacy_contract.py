#!/usr/bin/env python3
"""Check the native catalog against the pinned legacy TypeScript tool registry."""

import hashlib
import json
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
MANIFEST = HERE.parents[1] / "src/slic3r/GUI/McpToolsManifest.json"
ORACLE = HERE / "legacy_tools_oracle.json"
ORACLE_SHA256 = "0cac523c7f44107e1b73219904d2849bdec3967da7d060b9797995ac69dacd7f"


class LegacyContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert hashlib.sha256(ORACLE.read_bytes()).hexdigest() == ORACLE_SHA256
        cls.expected = json.loads(ORACLE.read_text(encoding="utf-8"))
        cls.actual = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_every_legacy_tool_has_its_exact_public_contract(self):
        self.assertEqual(len(self.expected), 90)
        self.assertEqual(len({tool["name"] for tool in self.expected}), 90)
        public = [{key: value for key, value in tool.items() if key != "method"}
                  for tool in self.actual]
        self.assertEqual(public, self.expected)

    def test_native_methods_are_unique_and_match_public_names(self):
        self.assertEqual(len(self.actual), 90)
        methods = [tool["method"] for tool in self.actual]
        self.assertEqual(len(set(methods)), 90)
        for tool in self.actual:
            self.assertEqual(tool["name"], tool["method"].replace(".", "_"))

    def test_safety_critical_public_contracts(self):
        tools = {tool["name"]: tool for tool in self.expected}
        for name in ("device_pause", "device_resume", "device_stop", "device_upload_start",
                     "device_print_start", "calibration_start"):
            self.assertTrue(tools[name]["annotations"]["openWorldHint"], name)
            self.assertFalse(tools[name]["annotations"]["readOnlyHint"], name)
        for name in ("device_stop", "device_upload_start", "device_print_start", "calibration_start"):
            self.assertIn("confirm", tools[name]["inputSchema"]["required"], name)
        self.assertIn("expectedPlateRevision", tools["slice_start"]["inputSchema"]["required"])
        self.assertIn("sliceResultId", tools["export_gcode"]["inputSchema"]["required"])
        self.assertNotIn("mcp_allow_device_actions",
                         tools["preferences_set"]["inputSchema"]["properties"]["key"]["enum"])


if __name__ == "__main__":
    unittest.main()
