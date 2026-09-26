#!/usr/bin/env python3
"""Local-only TAI64N reboot regression. Requires an isolated Linux netns.

Default: exit 0 only for reconnection within 30 seconds. --expect-rejection:
exit 0 only for a reproduced rejection followed by timestamp catch-up recovery.
Neither mode implements a clock fix. Uses no packages beyond Python's stdlib.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time


def run(*args, data=None):
    return subprocess.check_output(args, input=data, text=True).strip()


def timestamps(text):
    return re.findall(r"wg test: tai64n ([0-9a-f]{24})", text)


def verdict(expect_rejection, rejected, reconnected):
    success = rejected if expect_rejection else reconnected
    return (("REPRODUCED" if expect_rejection else "PASS") if success else "FAIL",
            0 if success else 1)


def packet(frame):
    if len(frame) < 42 or frame[12:14] != b"\x08\x00":
        return None
    ip = frame[14:]
    ihl = (ip[0] & 15) * 4
    if ip[0] >> 4 != 4 or ihl < 20 or len(ip) < ihl + 12:
        return None
    if ip[9] != 17 or int.from_bytes(ip[6:8], "big") & 0x3fff:
        return None
    src, dst = socket.inet_ntoa(ip[12:16]), socket.inet_ntoa(ip[16:20])
    sport, dport, length = struct.unpack("!HHH", ip[ihl:ihl + 6])
    if (src, dst, dport) == ("10.0.0.2", "10.0.0.1", 51821):
        direction = "nuttx"
    elif (src, dst, sport) == ("10.0.0.1", "10.0.0.2", 51821):
        direction = "linux"
    else:
        return None
    payload = ip[ihl + 8:ihl + length]
    if length < 12 or len(payload) != length - 8:
        return None
    kind = int.from_bytes(payload[:4], "little")
    if kind not in (1, 2, 3, 4):
        return None
    if kind in (1, 2, 3) and len(payload) != {1: 148, 2: 92, 3: 64}[kind]:
        return None
    if kind == 4 and len(payload) < 32:
        return None
    return {"direction": direction, "type": kind,
            "index": payload[4:8].hex(), "length": len(payload)}


class Harness:
    def __init__(self, root, temp):
        self.root, self.temp = root, temp
        self.proc = self.capture = self.console = None
        self.boot = self.serial = 0
        self.records = []
        self.commands = []
        self.logs = []

    def text(self):
        return self.logs[-1].read_text(errors="replace")

    def pump(self):
        if self.proc.poll() is not None:
            raise RuntimeError("sim exited unexpectedly")
        if self.capture:
            # Bound each drain so traffic cannot prevent deadline checks.
            for _ in range(256):
                try:
                    item = packet(self.capture.recv(65535))
                except BlockingIOError:
                    break
                if item:
                    item.update(boot=self.boot, elapsed=round(time.monotonic() - self.started, 3))
                    self.records.append(item)

    def wait(self, predicate, timeout, label):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if predicate():
                return
            time.sleep(.05)
        raise RuntimeError("timeout: " + label)

    def pause(self, seconds):
        end = time.monotonic() + seconds
        self.wait(lambda: time.monotonic() >= end, seconds + 1, "pause")

    def command(self, command, check=True):
        self.serial += 1
        tag = f"WGCHK_B{self.boot}_{self.serial}"
        start = len(self.text())
        self.proc.stdin.write(command + "\necho " + tag + ":$?\n")
        self.proc.stdin.flush()
        pattern = re.compile(r"^" + tag + r":([0-9]+)\r?$", re.M)
        self.wait(lambda: pattern.search(self.text()[start:]), 15, tag)
        output = self.text()[start:]
        status = int(pattern.search(output)[1])
        self.commands.append({"tag": tag, "status": status})
        if check and status:
            raise RuntimeError(f"{tag} exited {status}")
        return output

    def start(self, private, public):
        self.boot += 1
        path = self.temp / f"boot{self.boot}.log"
        self.logs.append(path)
        self.console = path.open("w")
        self.started = time.monotonic()
        self.proc = subprocess.Popen([str(self.root / "nuttx")], cwd=self.root,
                                     stdin=subprocess.PIPE, stdout=self.console,
                                     stderr=subprocess.STDOUT, text=True)
        self.wait(lambda: "nsh>" in self.text(), 15, "NSH prompt")
        run("ip", "addr", "replace", "10.0.0.1/24", "dev", "tap0")
        run("ip", "link", "set", "tap0", "up")
        self.capture = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
        self.capture.bind(("tap0", 0))
        self.capture.setblocking(False)
        self.command("wg set private-key " + private)
        self.command("wg set address 10.10.0.2/24")
        self.command(f"wg set peer {public} endpoint 10.0.0.1:51821 "
                     "allowed-ips 10.10.0.1/32 persistent-keepalive 5")

    def stop(self):
        if self.proc:
            if self.proc.poll() is None:
                self.proc.kill()
            self.proc.wait(timeout=10)
            self.proc.stdin.close()
            self.proc = None
        if self.capture:
            self.capture.close()
            self.capture = None
        if self.console:
            self.console.close()
            self.console = None

    def ping(self):
        out = self.command("ping -I wg0 -c 1 -W 1000 10.10.0.1", check=False)
        match = re.search(r"1 packets transmitted, (\d+) received", out)
        if not match:
            raise RuntimeError("missing ping summary")
        return int(match[1]) == 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/opt/nuttx"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-rejection", action="store_true")
    args = parser.parse_args()
    config = (args.root / ".config").read_text()
    required = ("NET_WIREGUARD_DEBUG_TAI64N", "SYSTEM_PING", "NET_BINDTODEVICE")
    if any(f"CONFIG_{name}=y" not in config for name in required):
        print("SKIP: enable timestamp logging, SYSTEM_PING, and NET_BINDTODEVICE")
        return 77
    if any(f"CONFIG_NET_WIREGUARD_DEBUG_{name}_STALL=y" in config for name in ("TX", "STOP")):
        raise RuntimeError("disable unrelated fault injection")
    if not all(shutil.which(name) for name in ("ip", "wg")):
        print("SKIP: ip/wg unavailable; no automatic downloads")
        return 77
    for interface in ("tap0", "wgreboot0"):
        if Path("/sys/class/net", interface).exists():
            raise RuntimeError("refusing to reuse existing " + interface)
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"mode": "reproduce" if args.expect_rejection else "regression"}
    created = False
    secrets = []
    result = 1
    with tempfile.TemporaryDirectory(prefix="wg-reboot-") as tmp:
        temp = Path(tmp)
        h = Harness(args.root.resolve(), temp)
        try:
            linux_private, nuttx_private = run("wg", "genkey"), run("wg", "genkey")
            secrets = [linux_private, nuttx_private]
            linux_public = run("wg", "pubkey", data=linux_private)
            nuttx_public = run("wg", "pubkey", data=nuttx_private)
            keyfile = temp / "linux.key"
            keyfile.write_text(linux_private)
            keyfile.chmod(0o600)
            run("ip", "link", "add", "wgreboot0", "type", "wireguard")
            created = True
            run("wg", "set", "wgreboot0", "private-key", str(keyfile),
                "listen-port", "51821", "peer", nuttx_public, "allowed-ips", "10.10.0.2/32")
            run("ip", "addr", "add", "10.10.0.1/24", "dev", "wgreboot0")
            run("ip", "link", "set", "wgreboot0", "up")
            ifindex = Path("/sys/class/net/wgreboot0/ifindex").read_text()

            def handshake():
                return int(run("wg", "show", "wgreboot0", "latest-handshakes").split()[1])

            print("Boot 1: aging the initiator before its first handshake", flush=True)
            h.start(nuttx_private, linux_public)
            h.pause(max(0, 70 - (time.monotonic() - h.started)))
            h.command("wg up")
            h.wait(lambda: handshake() > 0, 40, "initial handshake")
            h.wait(h.ping, 15, "initial tunnel ping")
            # Let NuttX keepalives acknowledge the Linux echo reply before
            # power loss; otherwise Linux's pending retry can mask the bug.
            h.pause(20)
            baseline = handshake()
            first = timestamps(h.text())
            if not first:
                raise RuntimeError("no generated timestamp evidence")
            # A single initiation avoids ambiguity about which timestamp was accepted.
            if len(first) != 1:
                raise RuntimeError("ambiguous baseline: rerun with one initial initiation")
            report.update(before_timestamp=first[0], before_handshake=baseline)
            h.stop()

            print("Boot 2: same key, unchanged Linux peer; observing 30 seconds", flush=True)
            h.start(nuttx_private, linux_public)
            h.command("wg up")
            h.pause(30)
            second = timestamps(h.text())
            records = [r for r in h.records if r["boot"] == 2]
            early_handshake = handshake()
            early_ping = h.ping()
            initiations = {r["index"] for r in records if r["direction"] == "nuttx" and r["type"] == 1}
            responses = [r for r in records if r["direction"] == "linux" and r["type"] == 2]
            rejected = (len(initiations) >= 2 and not responses and second
                        and max(second) < first[0] and early_handshake == baseline and not early_ping)
            reconnected = early_handshake > baseline and bool(responses) and early_ping
            report.update(window_seconds=30, early_timestamps=second,
                          early_initiations=len(initiations), early_responses=len(responses),
                          early_handshake=early_handshake, early_ping=early_ping,
                          rejection_reproduced=bool(rejected))
            if rejected:
                print("Rejection observed; waiting for timestamp catch-up control", flush=True)
                h.wait(lambda: handshake() > baseline, 100, "catch-up handshake")
                h.wait(h.ping, 15, "catch-up tunnel ping")
                if max(timestamps(h.text())) <= first[0]:
                    raise RuntimeError("recovery without timestamp catch-up")
                report["catch_up_recovered"] = True
            if any(r["direction"] == "linux" and r["type"] == 1 for r in h.records):
                raise RuntimeError("Linux initiated: test cannot isolate reboot rejection")
            if Path("/sys/class/net/wgreboot0/ifindex").read_text() != ifindex:
                raise RuntimeError("Linux interface changed")
            report["after_handshake"] = handshake()
            report["after_timestamps"] = timestamps(h.text())
            report["verdict"], result = verdict(args.expect_rejection, rejected, reconnected)
        except Exception as exc:
            report.update(verdict="ERROR", error=str(exc))
        finally:
            h.stop()
            if created:
                subprocess.run(["ip", "link", "del", "wgreboot0"], check=False)
            for log in h.logs:
                content = log.read_text(errors="replace")
                for secret in secrets:
                    content = content.replace(secret, "[REDACTED_PRIVATE_KEY]")
                (args.output / log.name).write_text(content)
            report.update(packets=h.records, commands=h.commands)
            (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(report["verdict"], report.get("error", ""), flush=True)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
