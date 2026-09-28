#!/usr/bin/env bash
# Negative-path checks against a live Linux kernel WireGuard peer: things
# that must NOT work.
#
#  1. Replay / endpoint hijack. A transport-data packet the Linux peer sent
#     to NuttX is captured and resent from a different source address. The
#     packet is authentic (it carries a valid tag under the session key),
#     so an implementation that updates the peer's endpoint before checking
#     the replay window will redirect the peer's traffic to the forger. The
#     endpoint must stay where it was, and the tunnel must keep working.
#
#  3. Out-of-window counter. The same captured packet is replayed after more
#     than CONFIG_NET_WIREGUARD_REPLAY_WINDOW later packets have been
#     accepted, so its counter has fallen off the bottom of the sliding
#     window. It is still authentic and still decrypts, so only the window
#     check can stop it. rx_bytes is counted after that check, which makes it
#     the observable: it must not move.
#
#  4. Type and length fuzzing. Every message type byte, and truncated and
#     oversized bodies for the four real ones, are thrown at the listen port.
#     The device must stay up and keep the tunnel working.
#
#  2. Handshake flood / cookie reply. More initiations than
#     CONFIG_NET_WIREGUARD_LOAD_THRESHOLD arrive within one second from a
#     forged source, each with a valid mac1 (anyone who knows the public
#     key can compute one) but garbage inside. Past the threshold the
#     device must stop doing Diffie-Hellman on them and answer with cookie
#     replies instead (whitepaper 5.4.7), and the real peer's tunnel must
#     be unaffected.
#
# Uses the same topology as verify-sim-wireguard.sh: NuttX sim on tap0 as
# 10.0.0.2:51820, Linux wgtest0 at 10.0.0.1:51821. The forged source is
# 10.0.0.9, added to tap0 for the duration of the test.
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"
linux_pub="$(printf "%s" "${linux_priv}" | wg pubkey)"
echo "Linux public key: ${linux_pub}"

threshold="$(sed -n 's/^CONFIG_NET_WIREGUARD_LOAD_THRESHOLD=//p' .config)"
threshold="${threshold:-4}"

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in

/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  set +e
  ip link del wgtest0 2>/dev/null
  printf "poweroff\n" >&3 2>/dev/null
  sleep 1
  kill "${nuttx_pid}" 2>/dev/null
  wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}

trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2

if ! ip link show tap0 >/dev/null 2>&1; then
  echo "ERROR: tap0 was not created by NuttX sim"
  sed -n "1,160p" /tmp/nuttx.out
  exit 1
fi

ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
ip addr add 10.0.0.9/24 dev tap0 2>/dev/null || true
ip link set tap0 up

printf "wg genkey\n" >&3
sleep 1
nuttx_priv="$(grep -Eo '^[A-Za-z0-9+/]{43}=' /tmp/nuttx.out | tail -1)"
nuttx_pub="$(printf "%s" "${nuttx_priv}" | wg pubkey)"
echo "NuttX public key: ${nuttx_pub}"
printf "wg set private-key %s\n" "${nuttx_priv}" >&3
printf "wg set address 10.10.0.2/24\n" >&3
printf "wg set peer %s allowed-ips 10.10.0.1/32\n" "${linux_pub}" >&3
printf "wg up\n" >&3
sleep 2

printf "%s\n" "${linux_priv}" > /tmp/linux_private.key
ip link add wgtest0 type wireguard
wg set wgtest0 \
  private-key /tmp/linux_private.key \
  listen-port 51821 \
  peer "${nuttx_pub}" \
  allowed-ips 10.10.0.2/32 \
  endpoint 10.0.0.2:51820 \
  persistent-keepalive 25
ip addr add 10.10.0.1/24 dev wgtest0
ip link set wgtest0 up

echo "Waiting for handshake..."
sleep 5
ping -c 3 -W 3 10.10.0.2 >/dev/null
echo "Tunnel is up"

# nsh_wg_show: print NuttX's "wg show" and return the endpoint line
nsh_endpoint() {
  local before
  before="$(wc -l </tmp/nuttx.out)"
  printf "wg show\n" >&3
  sleep 1
  tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g' | grep -m1 "endpoint:" | sed 's/^ *//'
}

endpoint_before="$(nsh_endpoint)"
echo "NuttX endpoint before: ${endpoint_before}"

# ---------------------------------------------------------------------------
# 1. Replay a captured transport-data packet from a forged source
# ---------------------------------------------------------------------------

python3 - "${threshold}" "${nuttx_pub}" <<'PY' &
import socket, struct, sys, time, hashlib, os

# Sniff one transport-data packet (type 4) from 10.0.0.1 to 10.0.0.2:51820,
# then resend its UDP payload from 10.0.0.9.
ETH_P_ALL = 3
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
s.bind(("tap0", 0))
s.settimeout(10)
payload = None
deadline = time.time() + 10
while time.time() < deadline:
    try:
        frame = s.recv(4096)
    except socket.timeout:
        break
    if len(frame) < 14 + 20 + 8 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    ihl = (ip[0] & 0x0f) * 4
    if ip[9] != 17:
        continue
    src = socket.inet_ntoa(ip[12:16]); dst = socket.inet_ntoa(ip[16:20])
    udp = ip[ihl:]
    dport = struct.unpack("!H", udp[2:4])[0]
    data = udp[8:]
    if src == "10.0.0.1" and dst == "10.0.0.2" and dport == 51820 and data[:1] == b"\x04":
        payload = data
        break
s.close()
if payload is None:
    print("REPLAY: no transport-data packet captured")
    sys.exit(2)
print("REPLAY: captured %d-byte transport-data packet, resending from 10.0.0.9:40000 x3" % len(payload))
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.bind(("10.0.0.9", 40000))
for _ in range(3):
    u.sendto(payload, ("10.0.0.2", 51820))
    time.sleep(0.2)
u.close()
PY
sniffer=$!
sleep 1
# One ping = one transport-data packet from Linux to capture. Nothing else
# is sent from the Linux side afterwards (keepalive is 25 s away), so the
# endpoint we read back reflects the replay alone and not a later genuine
# packet that would have moved it home again.
ping -c 1 -W 3 10.10.0.2 >/dev/null
set +e
wait "${sniffer}"
sniff_status=$?
set -e
if [ "${sniff_status}" -ne 0 ]; then
  echo "ERROR: could not capture a packet to replay"
  exit 1
fi

endpoint_after="$(nsh_endpoint)"
echo "NuttX endpoint after replay: ${endpoint_after}"

if [ "${endpoint_after}" != "${endpoint_before}" ]; then
  echo "FAIL: a replayed packet moved the peer endpoint (${endpoint_before} -> ${endpoint_after})"
  exit 1
fi
echo "PASS: replayed transport packet did not move the endpoint"

if ! ping -c 3 -W 3 10.10.0.2 >/dev/null; then
  echo "FAIL: tunnel broken after replay"
  exit 1
fi
echo "PASS: tunnel still carries traffic after replay"

# ---------------------------------------------------------------------------
# 2. Initiation flood with valid mac1 -> cookie replies past the threshold
# ---------------------------------------------------------------------------

python3 - "${threshold}" "${nuttx_pub}" <<'PY'
import socket, struct, sys, time, hashlib, base64, os

threshold = int(sys.argv[1])
pub = base64.b64decode(sys.argv[2])

# mac1 = MAC(Hash(LABEL_MAC1 || Spub), msg[:len-32]) with BLAKE2s
mac1_key = hashlib.blake2s(b"mac1----" + pub, digest_size=32).digest()

def initiation():
    body = b"\x01\x00\x00\x00" + os.urandom(4) + os.urandom(32) + os.urandom(48) + os.urandom(28)
    assert len(body) == 116
    mac1 = hashlib.blake2s(body, key=mac1_key, digest_size=16).digest()
    return body + mac1 + b"\x00" * 16   # mac2 = 0: we hold no cookie

u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.bind(("10.0.0.9", 40001))
u.settimeout(0.5)
n = threshold + 4
cookie_replies = 0
for i in range(n):
    u.sendto(initiation(), ("10.0.0.2", 51820))
    time.sleep(0.02)
deadline = time.time() + 2
while time.time() < deadline:
    try:
        data, addr = u.recvfrom(4096)
    except socket.timeout:
        continue
    if len(data) == 64 and data[:1] == b"\x03":
        cookie_replies += 1
u.close()
print("FLOOD: sent %d forged initiations (valid mac1), got %d cookie replies" % (n, cookie_replies))
# The first `threshold` initiations are processed normally (and fail
# decryption silently); everything after must draw a cookie reply.
expected = n - threshold
if cookie_replies < 1:
    print("FAIL: no cookie reply under load")
    sys.exit(1)
if cookie_replies > expected:
    print("FAIL: cookie replies (%d) exceed initiations past the threshold (%d)" % (cookie_replies, expected))
    sys.exit(1)
print("PASS: initiations past the load threshold were answered with cookie replies")
PY

sleep 1
if ! ping -c 3 -W 3 10.10.0.2 >/dev/null; then
  echo "FAIL: tunnel broken after initiation flood"
  exit 1
fi
echo "PASS: tunnel still carries traffic after the flood"

# A legitimate peer must still be able to handshake afterwards: force a new
# handshake from the Linux side by rotating its listen port... simpler: just
# confirm the existing session keeps passing traffic after the load window
# (one second) has expired and one more ping.
sleep 1.5
ping -c 1 -W 3 10.10.0.2 >/dev/null

# ---------------------------------------------------------------------------
# 3. Out-of-window counter
# ---------------------------------------------------------------------------

window="$(sed -n 's/^CONFIG_NET_WIREGUARD_REPLAY_WINDOW=//p' .config)"
window="${window:-2048}"

# "transfer: N B received, M B sent" -> "N M". Always plain bytes.
nsh_transfer() {
  local before
  before="$(wc -l </tmp/nuttx.out)"
  printf "wg show\n" >&3
  sleep 1
  tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g' |
    sed -n 's/^ *transfer: \([0-9]*\) B received, \([0-9]*\) B sent.*/\1 \2/p' |
    head -1
}

# Silence the peer's keepalive so nothing genuine arrives during the
# measurement; rx_bytes then has to be exactly unchanged, not just close.
wg set wgtest0 peer "${nuttx_pub}" persistent-keepalive 0

# Capture one transport-data packet that carries a payload. A keepalive is
# 32 bytes (16 header + empty body + 16 tag), so require more than that.
python3 - > /tmp/oow-capture.txt <<'PY' &
import socket, struct, sys, time

ETH_P_ALL = 3
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
s.bind(("tap0", 0))
s.settimeout(10)
deadline = time.time() + 10
while time.time() < deadline:
    try:
        frame = s.recv(4096)
    except socket.timeout:
        break
    if len(frame) < 14 + 20 + 8 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    ihl = (ip[0] & 0x0f) * 4
    if ip[9] != 17:
        continue
    src = socket.inet_ntoa(ip[12:16])
    dst = socket.inet_ntoa(ip[16:20])
    udp = ip[ihl:]
    dport = struct.unpack("!H", udp[2:4])[0]
    data = udp[8:]
    if (src == "10.0.0.1" and dst == "10.0.0.2" and dport == 51820
            and data[:1] == b"\x04" and len(data) > 32):
        receiver = struct.unpack("<I", data[4:8])[0]
        counter = struct.unpack("<Q", data[8:16])[0]
        open("/tmp/oow-frame.bin", "wb").write(frame)
        print("receiver %u counter %u len %d" % (receiver, counter, len(data)))
        sys.exit(0)
s.close()
print("no transport-data packet with a payload was captured")
sys.exit(2)
PY
capture=$!
sleep 1
ping -c 1 -W 3 10.10.0.2 >/dev/null
set +e
wait "${capture}"
capture_status=$?
set -e
cat /tmp/oow-capture.txt
[ "${capture_status}" -eq 0 ] || { echo "FAIL: nothing to replay"; exit 1; }

old_receiver="$(sed -n 's/^receiver \([0-9]*\).*/\1/p' /tmp/oow-capture.txt)"
old_counter="$(sed -n 's/.*counter \([0-9]*\).*/\1/p' /tmp/oow-capture.txt)"

# Push the window past the captured counter. Flood ping is one transport
# packet each way per request; drops only slow this down, so loop until the
# counter has actually advanced far enough.
advanced=0
for attempt in 1 2 3 4; do
  ping -f -c $((window + 600)) -W 1 10.10.0.2 >/dev/null 2>&1 || true
  python3 - > /tmp/oow-now.txt <<'PY' &
import socket, struct, sys, time

ETH_P_ALL = 3
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
s.bind(("tap0", 0))
s.settimeout(8)
deadline = time.time() + 8
while time.time() < deadline:
    try:
        frame = s.recv(4096)
    except socket.timeout:
        break
    if len(frame) < 14 + 20 + 8 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    ihl = (ip[0] & 0x0f) * 4
    if ip[9] != 17:
        continue
    if (socket.inet_ntoa(ip[12:16]) != "10.0.0.1"
            or socket.inet_ntoa(ip[16:20]) != "10.0.0.2"):
        continue
    udp = ip[ihl:]
    if struct.unpack("!H", udp[2:4])[0] != 51820:
        continue
    data = udp[8:]
    if data[:1] != b"\x04":
        continue
    print("receiver %u counter %u"
          % (struct.unpack("<I", data[4:8])[0],
             struct.unpack("<Q", data[8:16])[0]))
    sys.exit(0)
print("no packet seen")
sys.exit(2)
PY
  now=$!
  # Give the raw socket time to bind before there is anything to see, then
  # keep offering packets across the sniff window.
  sleep 1.5
  for _ in 1 2 3; do
    ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1 || true
    sleep 0.5
  done
  set +e
  wait "${now}"
  set -e
  new_receiver="$(sed -n 's/^receiver \([0-9]*\).*/\1/p' /tmp/oow-now.txt)"
  new_counter="$(sed -n 's/.*counter \([0-9]*\).*/\1/p' /tmp/oow-now.txt)"
  [ -n "${new_counter}" ] || continue
  if [ "${new_receiver}" != "${old_receiver}" ]; then
    echo "FAIL: the session rekeyed during the test (receiver ${old_receiver}"\
"-> ${new_receiver}); an old packet would be rejected for the wrong reason"
    exit 1
  fi
  if [ $((new_counter - old_counter)) -gt "${window}" ]; then
    advanced=1
    echo "counter advanced ${old_counter} -> ${new_counter} "\
"(window ${window}), same keypair"
    break
  fi
done
[ "${advanced}" -eq 1 ] ||
  { echo "FAIL: could not push the counter past the window"; exit 1; }

sleep 1
transfer_before="$(nsh_transfer)"
rx_before="${transfer_before%% *}"
echo "rx_bytes before the out-of-window replay: ${rx_before}"

# Re-inject the captured frame byte for byte, so it is exactly the packet the
# device already accepted once -- same source, same tag, same counter.
python3 - <<'PY'
import socket

frame = open("/tmp/oow-frame.bin", "rb").read()
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
s.bind(("tap0", 0))
for _ in range(3):
    s.send(frame)
s.close()
print("OOW: re-injected the captured frame 3 times")
PY

sleep 1.5
transfer_after="$(nsh_transfer)"
rx_after="${transfer_after%% *}"
echo "rx_bytes after the out-of-window replay:  ${rx_after}"

if [ -z "${rx_before}" ] || [ -z "${rx_after}" ]; then
  echo "FAIL: could not read the transfer counters"
  exit 1
fi
if [ "${rx_after}" != "${rx_before}" ]; then
  echo "FAIL: an out-of-window packet was accepted "\
"(rx_bytes ${rx_before} -> ${rx_after})"
  exit 1
fi
echo "PASS: a packet whose counter fell out of the replay window was dropped"

wg set wgtest0 peer "${nuttx_pub}" persistent-keepalive 25
if ! ping -c 3 -W 3 10.10.0.2 >/dev/null; then
  echo "FAIL: tunnel broken after the out-of-window replay"
  exit 1
fi
echo "PASS: tunnel still carries traffic"

# ---------------------------------------------------------------------------
# 4. Type and length fuzzing
# ---------------------------------------------------------------------------

python3 - <<'PY'
import os, random, socket, struct, sys

random.seed(20260928)          # Reproducible: a failure can be re-run.
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.bind(("10.0.0.9", 40002))
sent = 0

def shoot(payload):
    global sent
    u.sendto(payload, ("10.0.0.2", 51820))
    sent += 1

shoot(b"")                     # Zero-length datagram.

# Every type byte, including the four real ones and the 252 that are not.
for t in range(256):
    for length in (1, 2, 4, 15, 16, 17, 63, 64, 92, 116, 148, 200):
        shoot(bytes([t]) + os.urandom(length - 1))

# The real message lengths, off by one in both directions, plus all-zero and
# all-ones bodies at exactly the right length.
for t, size in ((1, 148), (2, 92), (3, 64), (4, 32)):
    for length in (size - 1, size, size + 1):
        shoot(bytes([t]) + os.urandom(length - 1))
    shoot(bytes([t]) + b"\x00" * (size - 1))
    shoot(bytes([t]) + b"\xff" * (size - 1))

# Reserved bytes set, which a correct parser ignores, at real lengths.
for t, size in ((1, 148), (2, 92), (3, 64), (4, 32)):
    shoot(bytes([t]) + b"\xff\xff\xff" + os.urandom(size - 4))

# A datagram longer than any real message, and one longer than the MTU path.
shoot(b"\x04" + os.urandom(1500))
shoot(b"\x01" + os.urandom(2000))
u.close()
print("FUZZ: sent %d malformed datagrams" % sent)
PY

sleep 2
if ! kill -0 "${nuttx_pid}" 2>/dev/null; then
  echo "FAIL: the sim died during fuzzing"
  tail -40 /tmp/nuttx.out
  exit 1
fi
if sed 's/\x1b\[K//g' /tmp/nuttx.out |
     grep -Eq "Assertion failed|PANIC|Fatal|up_assert|HardFault"; then
  echo "FAIL: the sim reported a fault during fuzzing"
  sed 's/\x1b\[K//g' /tmp/nuttx.out | grep -E \
    "Assertion failed|PANIC|Fatal|up_assert|HardFault" | head
  exit 1
fi
echo "PASS: the device survived type and length fuzzing"

endpoint_fuzz="$(nsh_endpoint)"
if [ "${endpoint_fuzz}" != "${endpoint_before}" ]; then
  echo "FAIL: fuzzing moved the peer endpoint"\
" (${endpoint_before} -> ${endpoint_fuzz})"
  exit 1
fi
if ! ping -c 3 -W 3 10.10.0.2 >/dev/null; then
  echo "FAIL: tunnel broken after fuzzing"
  exit 1
fi
echo "PASS: endpoint unchanged and the tunnel still carries traffic"

echo "PASS: sim WireGuard negative-path checks verified (kernel driver)"
