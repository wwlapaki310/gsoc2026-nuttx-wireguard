#!/usr/bin/env python3
"""Boot a sim kernel with the driver enabled and the wg command disabled."""
import pathlib
import re
import subprocess

root = pathlib.Path("/opt/nuttx")
config = (root / ".config").read_text().splitlines()
assert "CONFIG_NET_WIREGUARD=y" in config
assert "CONFIG_SYSTEM_WG=y" not in config
symbols = subprocess.check_output(["nm", str(root / "nuttx")], text=True)
assert re.search(r"\bT wireguard_initialize$", symbols, re.MULTILINE)
run = subprocess.run([str(root / "nuttx")], cwd=root,
                     input="ifconfig wg0\npoweroff\n", text=True,
                     capture_output=True, timeout=30)
assert run.returncode == 0, run.stdout + run.stderr
assert re.search(r"^wg0\s", run.stdout, re.MULTILINE), run.stdout + run.stderr
print("PASS: driver registers wg0 without apps/system/wg enabled")
