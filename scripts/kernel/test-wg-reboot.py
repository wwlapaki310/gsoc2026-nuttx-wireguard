#!/usr/bin/env python3
"""Host-side tests for reboot-test evidence parsing."""

import importlib.util
from pathlib import Path
import socket
import struct
import unittest

spec = importlib.util.spec_from_file_location(
    "reboot", Path(__file__).with_name("verify-sim-wg-reboot.py"))
reboot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reboot)


class EvidenceTests(unittest.TestCase):
    def frame(self, reverse=False):
        src, dst = ("10.0.0.2", "10.0.0.1")
        ports = (51820, 51821)
        if reverse:
            src, dst, ports = dst, src, ports[::-1]
        payload = struct.pack("<II", 2 if reverse else 1, 123) + bytes(84 if reverse else 140)
        ip = bytearray(20)
        ip[0], ip[9] = 0x45, 17
        ip[12:16], ip[16:20] = socket.inet_aton(src), socket.inet_aton(dst)
        return bytes(12) + b"\x08\x00" + ip + struct.pack("!HHHH", *ports, len(payload) + 8, 0) + payload

    def test_direction(self):
        self.assertEqual(reboot.packet(self.frame())["direction"], "nuttx")
        self.assertEqual(reboot.packet(self.frame(True))["direction"], "linux")

    def test_truncated(self):
        self.assertIsNone(reboot.packet(self.frame()[:-1]))
        self.assertIsNone(reboot.packet(b""))

    def test_timestamp_order(self):
        text = "wg test: tai64n 400000000000005000000000\n"
        before = reboot.timestamps(text)[0]
        self.assertLess("400000000000000b00000000", before)
        self.assertGreater("400000000000005100000000", before)
        self.assertEqual(reboot.timestamps("wg test: tai64n invalid"), [])

    def test_verdict_does_not_call_known_bug_pass(self):
        self.assertEqual(reboot.verdict(False, True, False), ("FAIL", 1))
        self.assertEqual(reboot.verdict(True, True, False), ("REPRODUCED", 0))
        self.assertEqual(reboot.verdict(False, False, True), ("PASS", 0))
        self.assertEqual(reboot.verdict(True, False, True), ("FAIL", 1))
        self.assertEqual(reboot.verdict(True, False, False), ("FAIL", 1))


if __name__ == "__main__":
    unittest.main()
