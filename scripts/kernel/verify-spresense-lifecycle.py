"""Hardware lifecycle soak: wg down / wg up on the real usrsock backend.

T7 did 100 down/up cycles in the sim, where the UDP socket is the host's. Here
each cycle closes and reopens a socket that belongs to the GS2200M module
through usrsock, and each wg up has to dial out and complete a fresh handshake
with the Windows peer over real Wi-Fi. That is the sequence the queued-output
design exists for, repeated.

Usage: spresense_lifecycle.py [COM6] [cycles]
"""
import re
import subprocess
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
CYCLES = int(sys.argv[2]) if len(sys.argv) > 2 else 40
TUNNEL = "10.11.0.2"


def clean(text):
    return re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", text).replace("\r", "")


class Board:
    def __init__(self, port):
        self.buf = []
        self.s = serial.Serial(port=port, baudrate=115200, timeout=1)

    def run(self, cmd, timeout=12.0):
        mark = len(clean("".join(self.buf)))
        self.s.write(("%s\n" % cmd).encode())
        self.s.flush()
        end = time.time() + timeout
        while time.time() < end:
            n = self.s.in_waiting
            if n:
                self.buf.append(self.s.read(n).decode(errors="replace"))
            else:
                text = clean("".join(self.buf))[mark:]
                if "nsh> " in text and text.rstrip().endswith(">"):
                    break
                time.sleep(0.05)
        return clean("".join(self.buf))[mark:]

    def close(self):
        self.s.close()


def reachable(tries=12):
    for _ in range(tries):
        out = subprocess.run(["ping", "-n", "2", "-w", "1500", TUNNEL],
                             capture_output=True, text=True)
        m = re.search(r"Received = (\d+)", out.stdout)
        if m and int(m.group(1)) > 0:
            return True
        time.sleep(1)
    return False


TASK = re.compile(r"([0-9a-f]{16})\s+(\d+)\s+(\d+)\s+([\d.]+)%\s+(\S+)")
HEAP = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\S*[Mm]em",
                  re.M)
IOB = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", re.M)


def sample(board, label):
    ps_out = board.run("ps")
    heap = HEAP.search(board.run("free"))
    iob = IOB.search(board.run("cat /proc/iobinfo"))
    rx = None
    for m in TASK.finditer(ps_out):
        if m.group(5) == "wg_rx":
            rx = (int(m.group(2)), int(m.group(3)), float(m.group(4)))
    print("%-22s heap %s allocs %s  iob %s/%s wait %s  wg_rx used %s"
          % (label,
             heap.group(2) if heap else "?", heap.group(6) if heap else "?",
             iob.group(2) if iob else "?", iob.group(1) if iob else "?",
             iob.group(3) if iob else "?",
             rx[1] if rx else "?"))
    return {"heap": int(heap.group(2)) if heap else None,
            "allocs": int(heap.group(6)) if heap else None,
            "iob_free": int(iob.group(2)) if iob else None,
            "iob_total": int(iob.group(1)) if iob else None,
            "iob_wait": int(iob.group(3)) if iob else None,
            "rx": rx}


board = Board(PORT)
failures = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        failures.append(what)


try:
    board.run("")                       # Wake the prompt.
    if not reachable():
        print("FAIL: the tunnel is not up before the soak starts")
        sys.exit(1)
    print("tunnel is up; %d down/up cycles on the real usrsock backend"
          % CYCLES)

    start = sample(board, "before")

    recovered = 0
    checked = 0
    faults = []
    for cycle in range(1, CYCLES + 1):
        down = board.run("wg down")
        up = board.run("wg up")
        for text in (down, up):
            if re.search(r"Assertion|assert|PANIC|Unhandled|HardFault", text):
                faults.append(text.strip()[:200])
        if cycle % 10 == 0:
            checked += 1
            if reachable():
                recovered += 1
                print("   cycle %d: traffic restored" % cycle)
            else:
                print("   cycle %d: NO traffic" % cycle)
        else:
            time.sleep(0.2)

    print("\nchecked recovery %d times, restored %d" % (checked, recovered))
    if not reachable():
        print("   final check: no traffic; waiting a little longer")
        time.sleep(10)
    final_ok = reachable()

    end = sample(board, "after %d cycles" % CYCLES)

    print("\n--- wg show ---")
    print("\n".join(l for l in board.run("wg show").splitlines() if l.strip()))

    print("\n=== verdict ===")
    check(not faults, "no assertion or fault was printed during the cycles")
    for f in faults[:3]:
        print("       " + f)
    check(recovered == checked,
          "traffic came back at every checkpoint (%d/%d)" % (recovered, checked))
    check(final_ok, "the tunnel is up at the end")
    if start["iob_free"] is not None and end["iob_free"] is not None:
        check(end["iob_free"] == end["iob_total"],
              "the IOB pool is whole afterwards (%d/%d)"
              % (end["iob_free"], end["iob_total"]))
        check(end["iob_wait"] == 0, "no task waited for an IOB")
    if start["allocs"] is not None and end["allocs"] is not None:
        check(end["allocs"] <= start["allocs"] + 4,
              "live allocations did not grow (%d -> %d)"
              % (start["allocs"], end["allocs"]))
        check(end["heap"] <= start["heap"] + 2048,
              "heap in use did not grow (%d -> %d)"
              % (start["heap"], end["heap"]))
    if start["rx"] and end["rx"]:
        check(end["rx"][1] <= start["rx"][1],
              "the wg_rx stack high-water did not grow (%d -> %d of %d)"
              % (start["rx"][1], end["rx"][1], end["rx"][0]))

    print("\nPASS: hardware lifecycle soak complete" if not failures
          else "\nFAIL: %d check(s) failed" % len(failures))
    sys.exit(1 if failures else 0)
finally:
    board.close()
