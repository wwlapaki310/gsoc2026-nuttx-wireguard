"""TE: does 'wg genkey' produce a different key after every restart?

Usage: python verify-spresense-entropy.py [COM6] [rounds]

The trap this looks for is a constant-seeded PRNG. NuttX's default
/dev/urandom is xorshift128 seeded from compile-time constants, so a board
left on that path hands out the *same* "random" private key on every boot --
a silent, total break of the key that no functional tunnel test would notice.
It is cheap to rule out, so it is worth ruling out rather than assuming.

Each round resets the board over DTR and reads one key. That is a reset, not
a power cycle; a compile-time constant seed shows up identically either way,
which is the failure mode being tested, but a seed that survives reset while
being lost on power-off would not be distinguished here. Three distinct keys
detect the catastrophic case; they say nothing about entropy quality.

Keys are never printed -- only their SHA-256 prefixes -- so the output is
safe to keep as evidence.
"""
import hashlib
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 3
B64 = re.compile(r"^[A-Za-z0-9+/]{43}=$", re.M)


def read_for(port, secs, buf):
    end = time.time() + secs
    while time.time() < end:
        pending = port.in_waiting
        if pending:
            buf.append(port.read(pending))
        else:
            time.sleep(0.05)


def clean(buf):
    text = b"".join(buf).decode(errors="replace")
    return re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", text).replace("\r", "")


keys = []
for round_index in range(ROUNDS):
    port = serial.Serial()
    port.port = PORT
    port.baudrate = 115200
    port.timeout = 1
    port.dtr = False
    port.rts = False
    port.open()
    port.dtr = True
    port.rts = True
    time.sleep(0.3)
    port.dtr = False
    port.rts = False
    port.reset_input_buffer()

    buf = []
    read_for(port, 26, buf)               # boot through rcS
    port.write(b"\n\nwg genkey\n")
    port.flush()
    read_for(port, 4, buf)
    port.close()

    # rcS prints the interface public key in its 'wg show' snapshot, so take
    # the first base64 line after the command echo, not the first in the log.
    tail = clean(buf).split("wg genkey", 1)[-1]
    found = B64.findall(tail)
    if not found:
        print("round %d: no key produced" % (round_index + 1))
        print(tail[-400:])
        sys.exit(1)

    keys.append(found[0])
    print("round %d: sha256 %s" % (round_index + 1,
                                   hashlib.sha256(found[0].encode())
                                   .hexdigest()[:16]))
    time.sleep(2)

unique = len(set(keys))
print()
print("distinct keys across %d restarts: %d" % (ROUNDS, unique))
if unique == ROUNDS:
    print("PASS: every restart produced a different key")
    sys.exit(0)
print("FAIL: a key repeated across restarts -- /dev/urandom is not seeded")
sys.exit(1)
