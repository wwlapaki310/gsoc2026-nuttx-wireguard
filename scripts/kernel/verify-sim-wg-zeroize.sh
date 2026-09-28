#!/usr/bin/env bash
# TZ: what is actually left in memory after "wg down".
#
# The original plan wording -- "a core dump finds 0 copies of the private key"
# -- cannot pass and should not: the static private key is deliberately kept so
# that "wg up" works again without reconfiguring, and the config file holds it
# anyway. What must not survive a down is the *session* material, because that
# is what forward secrecy rests on.
#
# So this reads the live sim process's memory and checks the fields by name:
#
#   kept on purpose   device private_key, peer public_key_dh, the label keys
#                     (all derived from the static identity, all needed again
#                     on the next up)
#   must be zero      sending_key and receiving_key of curr/prev/next keypair,
#                     and the handshake's ephemeral_private, chaining_key, hash
#
# The struct offsets are computed from the driver's own headers at run time, so
# this keeps testing the right bytes if the layout changes.
#
# The sim is an ordinary host process, but yama ptrace_scope=1 only lets a
# parent read a process's memory and containers do not carry CAP_SYS_PTRACE, so
# the driver below spawns the sim itself rather than being handed a pid.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

# Field offsets straight from the headers the driver compiles against.
cat > /tmp/wg_offsets.c <<'EOF'
#include <stdio.h>
#include <stddef.h>
#include "wg_noise.h"
int main(void)
{
  printf("max_peers %d\n", WG_MAX_PEERS);
  printf("key_len %d\n", WG_KEY_LEN);
  printf("dev_public_key %zu\n", offsetof(struct wg_device_s, public_key));
  printf("dev_private_key %zu\n", offsetof(struct wg_device_s, private_key));
  printf("dev_peers %zu\n", offsetof(struct wg_device_s, peers));
  printf("peer_size %zu\n", sizeof(struct wg_peer_s));
  printf("peer_public_key %zu\n", offsetof(struct wg_peer_s, public_key));
  printf("peer_public_key_dh %zu\n", offsetof(struct wg_peer_s, public_key_dh));
  printf("peer_curr %zu\n", offsetof(struct wg_peer_s, curr_keypair));
  printf("peer_prev %zu\n", offsetof(struct wg_peer_s, prev_keypair));
  printf("peer_next %zu\n", offsetof(struct wg_peer_s, next_keypair));
  printf("peer_handshake %zu\n", offsetof(struct wg_peer_s, handshake));
  printf("kp_sending_key %zu\n", offsetof(struct wg_keypair_s, sending_key));
  printf("kp_receiving_key %zu\n", offsetof(struct wg_keypair_s, receiving_key));
  printf("hs_ephemeral_private %zu\n",
         offsetof(struct wg_handshake_s, ephemeral_private));
  printf("hs_chaining_key %zu\n", offsetof(struct wg_handshake_s, chaining_key));
  printf("hs_hash %zu\n", offsetof(struct wg_handshake_s, hash));
  return 0;
}
EOF
gcc -o /tmp/wg_offsets /tmp/wg_offsets.c \
  -I/opt/nuttx/include -I/opt/nuttx/drivers/net/wireguard
/tmp/wg_offsets > /tmp/wg_offsets.txt
echo "struct offsets from the driver headers:"
sed 's/^/  /' /tmp/wg_offsets.txt

linux_priv="$(wg genkey)"; linux_pub="$(printf %s "${linux_priv}" | wg pubkey)"
# Generated here, so the raw bytes to look for in memory are known.
nuttx_priv="$(wg genkey)"; nuttx_pub="$(printf %s "${nuttx_priv}" | wg pubkey)"
printf %s "${linux_priv}" > /tmp/linux.key

cat > /tmp/wg_zeroize.py <<'PY'
import base64, os, re, socket, subprocess, sys, threading, time

offsets, nuttx_priv, nuttx_pub, linux_priv, linux_pub = sys.argv[1:6]
O = {}
for line in open(offsets):
    k, v = line.split()
    O[k] = int(v)
KEYLEN = O["key_len"]

priv_raw = base64.b64decode(nuttx_priv)
npub_raw = base64.b64decode(nuttx_pub)
lpub_raw = base64.b64decode(linux_pub)
assert len(priv_raw) == KEYLEN

def sh(*args, check=True):
    return subprocess.run(args, check=check, capture_output=True, text=True)

sim = subprocess.Popen(["/opt/nuttx/nuttx"], stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = []
def drain():
    for chunk in iter(lambda: sim.stdout.read(1), b""):
        out.append(chunk)
threading.Thread(target=drain, daemon=True).start()

def console():
    text = b"".join(out).decode("utf-8", "replace")
    return re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", text).replace("\r", "")

def send(cmd, wait=0.8):
    sim.stdin.write((cmd + "\n").encode()); sim.stdin.flush()
    time.sleep(wait)

failures = []
relay = None
hold_confirmation = False
held_transports = 0

def relay_packets():
    global held_transports
    while True:
        try:
            packet, sender = relay.recvfrom(65535)
            from_linux = sender[1] == 51821
            kind = int.from_bytes(packet[:4], "little")
            if hold_confirmation:
                if from_linux and kind == 4:
                    held_transports += 1
                    continue
                if not from_linux and kind == 1:
                    continue
            destination = ("10.0.0.2", 51820) if from_linux else \
                          ("10.0.0.1", 51821)
            relay.sendto(packet, destination)
        except OSError:
            return

def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures.append(what)

def cleanup():
    sh("ip", "link", "del", "wgtest0", check=False)
    if relay is not None:
        relay.close()
    try:
        send("poweroff", wait=1)
    except OSError:
        pass
    sim.kill(); sim.wait()

# ---- regions of the sim's writable memory ---------------------------------

def regions():
    result = []
    with open("/proc/%d/maps" % sim.pid) as maps, \
         open("/proc/%d/mem" % sim.pid, "rb", 0) as mem:
        for line in maps:
            m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S{4})", line)
            if not m or "w" not in m.group(3):
                continue
            lo, hi = int(m.group(1), 16), int(m.group(2), 16)
            if hi - lo > 512 * 1024 * 1024:
                continue
            try:
                mem.seek(lo)
                blob = mem.read(hi - lo)
            except (OSError, ValueError, OverflowError):
                continue
            if len(blob) == hi - lo:
                result.append((lo, blob))
    return result

def occurrences(blob, needle):
    at, start = [], 0
    while True:
        i = blob.find(needle, start)
        if i < 0:
            return at
        at.append(i)
        start = i + 1

def find_device(regs):
    """The wg_device_s whose private and public key are the ones we set."""
    found = []
    for lo, blob in regs:
        for i in occurrences(blob, priv_raw):
            base = i - O["dev_private_key"]
            if base < 0 or base + O["dev_peers"] > len(blob):
                continue
            pub = blob[base + O["dev_public_key"]:
                       base + O["dev_public_key"] + KEYLEN]
            if pub == npub_raw:
                found.append((lo, blob, base))
    return found

def peer_base(blob, dev, index):
    return dev + O["dev_peers"] + index * O["peer_size"]

def find_peer(blob, dev):
    for index in range(O["max_peers"]):
        base = peer_base(blob, dev, index)
        if base + O["peer_size"] > len(blob):
            continue
        pub = blob[base + O["peer_public_key"]:
                   base + O["peer_public_key"] + KEYLEN]
        if pub == lpub_raw:
            return index, base
    return None, None

def field(blob, base, off, length=None):
    length = KEYLEN if length is None else length
    return blob[base + off: base + off + length]

def secrets(blob, peer):
    """The session secrets, by name, as they currently sit in memory."""
    result = {}
    for name, koff in (("curr", O["peer_curr"]), ("prev", O["peer_prev"]),
                       ("next", O["peer_next"])):
        kp = peer + koff
        result[name + ".sending_key"] = field(blob, kp, O["kp_sending_key"])
        result[name + ".receiving_key"] = field(blob, kp, O["kp_receiving_key"])
    hs = peer + O["peer_handshake"]
    result["handshake.ephemeral_private"] = \
        field(blob, hs, O["hs_ephemeral_private"])
    result["handshake.chaining_key"] = field(blob, hs, O["hs_chaining_key"])
    result["handshake.hash"] = field(blob, hs, O["hs_hash"])
    return result

def snapshot(label):
    regs = regions()
    devices = find_device(regs)
    copies = sum(len(occurrences(blob, priv_raw)) for _, blob in regs)
    print("%s: %d live wg_device_s, %d raw copies of the static key"
          % (label, len(devices), copies))
    if not devices:
        return None, copies
    lo, blob, dev = devices[0]
    index, peer = find_peer(blob, dev)
    if peer is None:
        print("  the configured peer is not in the device's peer array")
        return None, copies
    values = secrets(blob, peer)
    for name in sorted(values):
        live = sum(1 for b in values[name] if b)
        print("  %-30s %s" % (name, "all zero" if live == 0
                              else "%d/%d bytes set" % (live, KEYLEN)))
    kept = {"private_key": field(blob, dev, O["dev_private_key"]),
            "peer.public_key_dh": field(blob, peer, O["peer_public_key_dh"])}
    return (values, kept), copies

try:
    # ---- bring up a real tunnel so session material exists ---------------
    for _ in range(50):
        if subprocess.run(["ip", "link", "show", "tap0"],
                          capture_output=True).returncode == 0:
            break
        time.sleep(0.2)
    else:
        raise SystemExit("FAIL: tap0 was never created")
    sh("ip", "addr", "add", "10.0.0.1/24", "dev", "tap0", check=False)
    sh("ip", "link", "set", "tap0", "up")
    relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    relay.bind(("10.0.0.1", 51822))
    threading.Thread(target=relay_packets, daemon=True).start()

    send("wg set private-key " + nuttx_priv)
    send("wg set address 10.10.0.2/24")
    send("wg set peer %s endpoint 10.0.0.1:51822 allowed-ips 10.10.0.1/32 "
         "persistent-keepalive 5" % linux_pub)
    send("wg up")

    # No responder exists yet: initiation must leave real ephemeral material
    # resident, rather than testing fields which happened to be zero already.
    pending = None
    hs_names = ("handshake.ephemeral_private", "handshake.chaining_key",
                "handshake.hash")
    for _ in range(15):
        pending, _ = snapshot("unanswered initiation")
        if pending and all(any(pending[0][n]) for n in hs_names):
            break
        time.sleep(1)
    if not pending or not all(any(pending[0][n]) for n in hs_names):
        raise SystemExit("FAIL: did not observe nonzero handshake secrets")
    send("wg down", wait=2)
    cleared, _ = snapshot("down after unanswered initiation")
    for name in hs_names:
        check(cleared is not None and not any(cleared[0][name]),
              "%s: observed nonzero, then zero after down" % name)
    send("wg up")

    def add_linux_peer():
        sh("wg", "set", "wgtest0", "peer", nuttx_pub,
           "allowed-ips", "10.10.0.2/32", "endpoint", "10.0.0.1:51822",
           "persistent-keepalive", "5")

    sh("ip", "link", "add", "wgtest0", "type", "wireguard")
    sh("wg", "set", "wgtest0", "private-key", "/tmp/linux.key",
       "listen-port", "51821")
    add_linux_peer()
    sh("ip", "addr", "add", "10.10.0.1/24", "dev", "wgtest0")
    sh("ip", "link", "set", "wgtest0", "up")

    def tunnel_up(tries=15):
        for _ in range(tries):
            time.sleep(2)
            if subprocess.run(["ping", "-c", "1", "-W", "2", "10.10.0.2"],
                              capture_output=True).returncode == 0:
                return True
        return False

    if not tunnel_up():
        raise SystemExit("FAIL: the tunnel never came up, "
                         "so there is no session material to clear")
    subprocess.run(["ping", "-c", "5", "-i", "0.3", "-W", "2", "10.10.0.2"],
                   capture_output=True)

    # Let the real rekey timers rotate the session. Do not delete/re-add a
    # peer or manipulate either clock: those bypass the timer path.
    def latest_handshake():
        output = sh("wg", "show", "wgtest0", "latest-handshakes").stdout
        rows = [line.split() for line in output.splitlines()]
        return next((int(row[1]) for row in rows if row[0] == nuttx_pub), 0)

    original_handshake = latest_handshake()
    check(original_handshake > 0, "initial Linux handshake timestamp exists")
    before_rotation, _ = snapshot("before natural rekey")
    deadline = time.monotonic() + 210
    rotated = False
    while time.monotonic() < deadline:
        # Ignore overlapping startup handshakes. The configured rekey age is
        # 120s; allow scheduling/sampling margin but not an immediate retry.
        if latest_handshake() - original_handshake >= 110:
            print("natural handshake timestamp advanced by %ds" %
                  (latest_handshake() - original_handshake))
            rotated = tunnel_up()
            break
        subprocess.run(["ping", "-c", "1", "-W", "2", "10.10.0.2"],
                       capture_output=True)
        time.sleep(2)
    check(rotated, "natural rekey completed without peer reset or clock change")
    subprocess.run(["ping", "-c", "5", "-i", "0.3", "-W", "2", "10.10.0.2"],
                   capture_output=True)

    # ---- while up: the session secrets must be present ------------------
    up_state, up_copies = snapshot("while up")
    if up_state is None:
        raise SystemExit("FAIL: could not locate the device in memory; "
                         "the probe is wrong, not the driver")
    up_values, up_kept = up_state
    check(before_rotation is not None and
          before_rotation[0]["curr.sending_key"] != up_values["curr.sending_key"],
          "natural rekey changed the actual current sending key")
    check(all(any(up_values[n]) for n in
              ("curr.sending_key", "curr.receiving_key",
               "prev.sending_key", "prev.receiving_key")),
          "current and previous keypairs were both observed nonzero")
    loaded = sorted(n for n in up_values if any(up_values[n]))
    check(bool(loaded),
          "session material is present while up (otherwise nothing is proven)")
    check(rotated, "a second handshake completed, rotating the keypairs")
    check(any(up_kept["private_key"]), "the static key is resident while up")

    # ---- after down: sessions gone, identity kept -----------------------
    mark = len(console())
    send("wg down", wait=2)
    down_state, down_copies = snapshot("after down")
    if down_state is None:
        raise SystemExit("FAIL: the device disappeared from memory after down")
    down_values, down_kept = down_state

    for name in sorted(down_values):
        if name in loaded:
            check(not any(down_values[name]), "%s is zeroized by down" % name)
        elif any(down_values[name]):
            check(False, "%s was empty while up but is set after down" % name)
        else:
            # Say so rather than counting it as evidence: a field that never
            # held a key proves nothing about clearing one.
            print("  --   %s held no key while up, not exercised" % name)

    check(any(down_kept["private_key"]),
          "the static key is kept, so 'wg up' still works without "
          "reconfiguring")
    check(down_copies <= up_copies,
          "no extra copies of the static key were left behind "
          "(%d -> %d)" % (up_copies, down_copies))

    # ---- and the consequence: re-up must do a fresh handshake -----------
    # Drop the Linux side's session too, so nothing on either end could carry
    # traffic without a new handshake.
    sh("wg", "set", "wgtest0", "peer", nuttx_pub, "remove")
    add_linux_peer()
    send("wg up")
    check(tunnel_up(), "the tunnel comes back after down/up")
    send("wg show", wait=1.5)
    check("latest handshake" in console()[mark:],
          "the re-up reports a handshake of its own, not a resumed session")
    reborn, _ = snapshot("after re-up")
    check(reborn is not None and
          any(any(v) for v in reborn[0].values()),
          "fresh session material exists after the re-up")

    # Force the responder's pending-next state using only network delivery:
    # suppress NuttX initiations and Linux transport confirmations, but pass
    # Linux initiation and NuttX response. No driver state is patched.
    send("wg down", wait=2)
    sh("wg", "set", "wgtest0", "peer", nuttx_pub, "remove")
    hold_confirmation = True
    send("wg up")
    add_linux_peer()
    next_names = ("next.sending_key", "next.receiving_key")
    pending_next = None
    for _ in range(20):
        subprocess.run(["ping", "-c", "1", "-W", "1", "10.10.0.2"],
                       capture_output=True)
        pending_next, _ = snapshot("unconfirmed responder session")
        if pending_next and all(any(pending_next[0][n]) for n in next_names):
            break
        time.sleep(1)
    if not pending_next or not all(any(pending_next[0][n]) for n in next_names):
        raise SystemExit("FAIL: never observed nonzero pending-next keys")
    check(held_transports > 0, "relay withheld actual Linux confirmations")
    send("wg down", wait=2)
    cleared_next, _ = snapshot("down after pending-next session")
    for name in next_names:
        check(cleared_next is not None and not any(cleared_next[0][name]),
              "%s: observed nonzero, then zero after down" % name)
finally:
    tail = console()
    cleanup()

if failures:
    print("FAIL: %d check(s) failed" % len(failures))
    for f in failures:
        print("  - " + f)
    print(tail[-2000:])
    sys.exit(1)
print("PASS: sessions are zeroized on down, the static identity is kept (TZ)")
PY

python3 /tmp/wg_zeroize.py /tmp/wg_offsets.txt \
  "${nuttx_priv}" "${nuttx_pub}" "${linux_priv}" "${linux_pub}"
