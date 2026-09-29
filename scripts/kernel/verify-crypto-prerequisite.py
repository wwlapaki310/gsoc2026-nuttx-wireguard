#!/usr/bin/env python3
"""Boot a crypto-only sim build and require the startup KAT success message."""
import pathlib
import subprocess

root = pathlib.Path("/opt/nuttx")
config = (root / ".config").read_text().splitlines()
assert "CONFIG_CRYPTO_ALGTEST=y" in config
assert "CONFIG_DEBUG_CRYPTO_INFO=y" in config
assert "CONFIG_NET_WIREGUARD=y" not in config
assert not (root / "drivers/net/wireguard").exists()
run = subprocess.run([str(root / "nuttx")], cwd=root, input="poweroff\n",
                     text=True, capture_output=True, timeout=30)
assert run.returncode == 0, run.stdout + run.stderr
assert "crypto test OK" in run.stdout, run.stdout + run.stderr
print("PASS: crypto prerequisite boots and passes startup KAT without driver")
