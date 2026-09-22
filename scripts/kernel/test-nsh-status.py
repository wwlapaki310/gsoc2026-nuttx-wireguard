#!/usr/bin/env python3
"""Host-only regression tests for the lifecycle test's completion oracle."""

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "nsh_status", Path(__file__).with_name("nsh-status.py")
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StatusTests(unittest.TestCase):
    tags = ["WGCHK_D1", "WGCHK_U1"]

    def test_completed(self):
        self.assertEqual(module.check_status(
            "nsh> echo WGCHK_D1:$?\r\nWGCHK_D1:0\r\n"
            "nsh> echo WGCHK_U1:$?\r\n\x1b[KWGCHK_U1:0\r\n", self.tags), 0)

    def test_input_echo_is_not_completion(self):
        self.assertEqual(module.check_status(
            "nsh> echo WGCHK_D1:0\n", self.tags), 2)

    def test_failed_command(self):
        self.assertEqual(module.check_status("WGCHK_D1:1\n", self.tags), 1)

    def test_reordered(self):
        self.assertEqual(module.check_status(
            "WGCHK_U1:0\nWGCHK_D1:0\n", self.tags), 1)

    def test_duplicate(self):
        self.assertEqual(module.check_status(
            "WGCHK_D1:0\nWGCHK_D1:0\n", self.tags), 1)

    def test_incomplete(self):
        self.assertEqual(module.check_status("WGCHK_D1:0\n", self.tags), 2)

    def test_partial_line(self):
        self.assertEqual(module.check_status("WGCHK_D1:", self.tags), 2)

    def test_unterminated_status(self):
        self.assertEqual(module.check_status("WGCHK_D1:0", self.tags), 2)

    def test_expected_failure(self):
        self.assertEqual(module.check_status("WGCHK_D1:1\n", ["WGCHK_D1=1"]), 0)

    def test_unexpected_success(self):
        self.assertEqual(module.check_status("WGCHK_D1:0\n", ["WGCHK_D1=1"]), 1)


if __name__ == "__main__":
    unittest.main()
