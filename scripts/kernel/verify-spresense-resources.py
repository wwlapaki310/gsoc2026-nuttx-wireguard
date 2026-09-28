"""Hardware resource measurement for the in-kernel WireGuard driver.

The sim soak (T7) gave a clean answer -- heap and the live allocation count
return exactly to their boot values, the IOB pool stays whole, and the wg_rx
stack high-water settles at 2536 bytes -- but the sim's allocator is the host's
and its frames are 64-bit. None of the heap numbers transfer to a board, and
the stack number transfers only as an upper bound.

So: the same three metrics on the Spresense, on a Cortex-M4F, over real Wi-Fi
through GS2200M/usrsock, before and after load driven from this host.

Boots headless (rcS brings up Wi-Fi, loads the saved configuration and brings
wg0 up), so nothing is typed until the tunnel already exists.

Every command is read until the shell prompt comes back, because a fixed wait
truncates ps and free often enough to make the numbers unusable.
"""
import re
import subprocess
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
LOAD_SECONDS = int(sys.argv[2]) if len(sys.argv) > 2 else 120
TUNNEL = "10.11.0.2"
BOOT_WAIT = 38


def clean(text):
    return re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", text).replace("\r", "")


class Board:
    def __init__(self, port):
        self.buf = []
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 1
        self.s.dtr = False
        self.s.rts = False
        self.s.open()

    def reset(self):
        self.s.dtr = True
        self.s.rts = True
        time.sleep(0.3)
        self.s.dtr = False
        self.s.rts = False
        self.s.reset_input_buffer()

    def read_for(self, secs):
        end = time.time() + secs
        while time.time() < end:
            n = self.s.in_waiting
            if n:
                self.buf.append(self.s.read(n).decode(errors="replace"))
            else:
                time.sleep(0.05)

    def run(self, cmd, timeout=12.0):
        """Send cmd and return everything up to the next prompt."""
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
                # The echoed command, then output, then a fresh prompt.
                if text.count("nsh> ") >= 1 and text.rstrip().endswith(">"):
                    break
                time.sleep(0.05)
        return clean("".join(self.buf))[mark:]

    def close(self):
        self.s.close()


def ping_batch(count, size):
    """Return (sent, received) as Windows ping reports them."""
    out = subprocess.run(["ping", "-n", str(count), "-l", str(size),
                          "-w", "1500", TUNNEL],
                         capture_output=True, text=True)
    m = re.search(r"Sent = (\d+), Received = (\d+)", out.stdout)
    if not m:
        m = re.search(r"= (\d+).*?= (\d+)", out.stdout)
    return (int(m.group(1)), int(m.group(2))) if m else (count, 0)


TASK = re.compile(r"([0-9a-f]{16})\s+(\d+)\s+(\d+)\s+([\d.]+)%\s+(\S+)")
HEAP = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\S*[Mm]em",
                  re.M)
IOB = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", re.M)


def sample(board, label):
    ps_out = board.run("ps")
    free_out = board.run("free")
    iob_out = board.run("cat /proc/iobinfo")

    stacks = {}
    for m in TASK.finditer(ps_out):
        stacks[m.group(5)] = (int(m.group(2)), int(m.group(3)),
                              float(m.group(4)))
    heap = HEAP.search(free_out)
    iob = IOB.search(iob_out)

    print("\n== %s" % label)
    print("   heap: used %s, allocs %s" % (heap.group(2), heap.group(6))
          if heap else "   heap: UNPARSED: %r" % free_out[-200:])
    print("   iob : %s free of %s, %s waiting, %s throttled"
          % (iob.group(2), iob.group(1), iob.group(3), iob.group(4))
          if iob else "   iob : UNPARSED: %r" % iob_out[-200:])
    for name in sorted(stacks):
        size, used, filled = stacks[name]
        print("   %-16s stack %5d  used %5d  %5.1f%%" %
              (name, size, used, filled))

    return {"heap": int(heap.group(2)) if heap else None,
            "allocs": int(heap.group(6)) if heap else None,
            "iob_free": int(iob.group(2)) if iob else None,
            "iob_total": int(iob.group(1)) if iob else None,
            "iob_wait": int(iob.group(3)) if iob else None,
            "iob_throttle": int(iob.group(4)) if iob else None,
            "stacks": stacks}


board = Board(PORT)
failures = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        failures.append(what)


try:
    print("resetting the board; headless bring-up takes about %ds" % BOOT_WAIT)
    board.reset()
    board.read_for(BOOT_WAIT)
    boot = clean("".join(board.buf))
    if "wg0" not in boot:
        print("FAIL: rcS did not report wg0")
        print(boot[-1500:])
        sys.exit(1)
    print("--- boot tail ---")
    print("\n".join(l for l in boot.splitlines()[-12:] if l.strip()))

    sent, got = ping_batch(4, 32)
    if got == 0:
        time.sleep(8)
        sent, got = ping_batch(4, 32)
    if got == 0:
        print("FAIL: no tunnelled traffic to %s" % TUNNEL)
        sys.exit(1)
    print("\nPASS: tunnel answers from this host (%d/%d)" % (got, sent))

    idle = sample(board, "after bring-up, tunnel idle")

    # Several senders at once, and payloads that fill the tunnel MTU, so the
    # driver's buffers see more than the keepalive-sized traffic everything
    # else here produces.
    print("\ndriving load for %ds from this host" % LOAD_SECONDS)
    totals = {"sent": 0, "got": 0}
    lock = threading.Lock()
    stop = time.time() + LOAD_SECONDS

    def worker(size):
        while time.time() < stop:
            s, g = ping_batch(25, size)
            with lock:
                totals["sent"] += s
                totals["got"] += g

    threads = [threading.Thread(target=worker, args=(size,), daemon=True)
               for size in (1000, 1000, 200)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print("   %d of %d pings answered through the tunnel"
          % (totals["got"], totals["sent"]))

    loaded = sample(board, "under load")

    print("\n--- wg show ---")
    print("\n".join(l for l in board.run("wg show").splitlines() if l.strip()))

    time.sleep(6)
    rest = sample(board, "after the load stopped")

    print("\n=== verdict ===")
    check(totals["got"] > 100,
          "the tunnel carried real traffic (%d answered)" % totals["got"])
    for name, s in (("idle", idle), ("loaded", loaded), ("rest", rest)):
        check(s["iob_free"] is not None and s["heap"] is not None,
              "the %s sample parsed" % name)
    if all(s["iob_free"] is not None for s in (idle, loaded, rest)):
        check(rest["iob_free"] == rest["iob_total"],
              "the IOB pool is whole at rest (%s/%s)"
              % (rest["iob_free"], rest["iob_total"]))
        check(all(s["iob_wait"] == 0 for s in (idle, loaded, rest)),
              "no task ever waited for an IOB")
        print("       iob free: %d -> %d -> %d of %d"
              % (idle["iob_free"], loaded["iob_free"], rest["iob_free"],
                 rest["iob_total"]))
    if all(s["allocs"] is not None for s in (idle, rest)):
        check(rest["allocs"] <= idle["allocs"] + 4,
              "live allocations return to the idle count (%d -> %d)"
              % (idle["allocs"], rest["allocs"]))
        print("       heap used: %d -> %d -> %d"
              % (idle["heap"], loaded["heap"], rest["heap"]))
    rx = [s["stacks"].get("wg_rx") for s in (idle, loaded, rest)]
    if all(rx):
        print("       wg_rx stack high-water: %d -> %d -> %d of %d bytes"
              % (rx[0][1], rx[1][1], rx[2][1], rx[0][0]))
        check(rx[2][1] == rx[1][1],
              "the wg_rx high-water does not move after the load")
        check(rx[2][2] < 80.0,
              "the wg_rx stack keeps headroom (worst %.1f%% filled)" % rx[2][2])
    else:
        check(False, "the wg_rx thread appears in ps")

    print("\nPASS: hardware resource measurement complete" if not failures
          else "\nFAIL: %d check(s) failed" % len(failures))
    sys.exit(1 if failures else 0)
finally:
    board.close()
