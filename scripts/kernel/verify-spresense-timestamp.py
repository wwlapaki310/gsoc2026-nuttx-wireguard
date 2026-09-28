"""TT on hardware: the timestamp problem (#14), as a controlled pair.

WireGuard's responder drops any handshake timestamp at or below the greatest it
has already accepted for that key, so an initiator must beat its own past across
a reboot. A board without a retained clock cannot. That is #14, and until now it
was argued from the protocol and a sim reproduction.

The measurement is hard to do honestly for one reason: if anything sends traffic
to the board, the *peer* initiates, and a responder needs no timestamp of its
own. So a naive "can I ping it after a reboot" test passes and proves nothing --
which is exactly what happened on the first attempt here, 16 s to recover.

So this runs two arms that differ in one thing, and sends the board nothing in
either:

  A. reset, leave the clock as it boots, watch the board's own view of the
     session for 75 s
  B. reset, set the clock from this host, bring the interface down and up,
     watch the same way

Identical board, peer, image and traffic (none). If A never gets a handshake and
B does, the refused timestamp is the difference.

"wg show" counts transport data, not handshake attempts, so its counters cannot
distinguish "refused" from "never tried" on their own. That is what arm B is
for.

Usage: verify-spresense-timestamp.py [COM6]
"""
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BOOT_WAIT = 38
WATCH = 75

HANDSHAKE = re.compile(r"latest handshake: (.+)")
TRANSFER = re.compile(r"transfer: (\d+) B received, (\d+) B sent")
WARNING = "realtime clock looks unset"


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
        self.buf = []

    def read_for(self, secs):
        end = time.time() + secs
        while time.time() < end:
            n = self.s.in_waiting
            if n:
                self.buf.append(self.s.read(n).decode(errors="replace"))
            else:
                time.sleep(0.05)

    def run(self, cmd, timeout=12.0):
        """Send cmd and return output up to the next prompt."""
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


def date_of(board):
    out = board.run("date").strip().splitlines()
    return out[-2].strip() if len(out) >= 2 else "?"


def watch(board, label):
    """Poll the board's own session state; send it nothing."""
    print("   watching for %ds, sending the board nothing" % WATCH)
    start = time.time()
    last = ""
    while time.time() - start < WATCH:
        out = board.run("wg show")
        hs = HANDSHAKE.search(out)
        tx = TRANSFER.search(out)
        state = "%s | %s" % (hs.group(1).strip() if hs else "?",
                             ("rx %s tx %s" % tx.groups()) if tx else "?")
        if state != last:
            print("     %5.1fs  %s" % (time.time() - start, state))
            last = state
        if hs and "never" not in hs.group(1):
            return time.time() - start
        time.sleep(4)
    return None


board = Board(PORT)
failures = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        failures.append(what)


try:
    print("=== arm A: clock as it boots ===")
    board.reset()
    board.read_for(BOOT_WAIT)
    boot_a = clean("".join(board.buf))
    warned_a = WARNING in boot_a
    date_a = date_of(board)
    print("   clock: %s" % date_a)
    print("   driver warned about the clock: %s" % warned_a)
    a = watch(board, "A")

    print("\n=== arm B: same, with the clock set from this host ===")
    board.reset()
    board.read_for(BOOT_WAIT)
    boot_b = clean("".join(board.buf))
    now = time.strftime("%b %d %H:%M:%S %Y", time.gmtime())
    board.run('date -s "%s"' % now)
    print("   clock: %s (set to %s UTC)" % (date_of(board), now))
    mark = len(clean("".join(board.buf)))
    board.run("wg down")
    board.run("wg up")
    warned_b = WARNING in clean("".join(board.buf))[mark:]
    print("   driver warned about the clock: %s" % warned_b)
    b = watch(board, "B")

    print("\n=== result ===")
    print("   arm A (clock unset): %s"
          % ("no handshake in %ds" % WATCH if a is None else "%.1fs" % a))
    print("   arm B (clock set)  : %s"
          % ("no handshake in %ds" % WATCH if b is None else "%.1fs" % b))

    check(warned_a, "the driver warns when the clock has never been set")
    check(not warned_b, "the warning is silent once the clock is set")
    check(b is not None,
          "with a clock the board dials out and the peer accepts it")
    check(a is None,
          "without a clock the board cannot re-establish as initiator")

    if a is None and b is not None:
        print("\n   The only difference between the arms is the clock, so the"
              "\n   refused timestamp is the cause. This is #14's open case"
              "\n   measured on hardware, and also the evidence that the design"
              "\n   is right wherever a clock exists.")
        print("\n   Note on why this needs care: anything that sends traffic to"
              "\n   the board makes the *peer* initiate, and a responder needs"
              "\n   no timestamp of its own. A ping-after-reboot test therefore"
              "\n   passes while the device is still broken as a dial-out peer.")

    print("\nPASS: hardware timestamp behaviour measured" if not failures
          else "\nFAIL: %d check(s) failed" % len(failures))
    sys.exit(1 if failures else 0)
finally:
    board.close()
