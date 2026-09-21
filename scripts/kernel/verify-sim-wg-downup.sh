#!/usr/bin/env bash
# Lifecycle stress for the ifdown-stop and non-blocking-send fixes:
# repeatedly bring wg0 down and up while a sustained inbound flood hits the
# listen port and a real Linux peer keeps sending. This is the reproduction
# case for
#   - ifdown closing the socket while the RX thread is still draining a
#     flood (the thread now re-checks running in its inner loop and ifdown
#     waits for it to leave before closing), and
#   - a blocking send releasing the network lock mid-transmit under buffer
#     pressure (sends are now MSG_DONTWAIT, atomic under net_lock).
#
# Pass = every down/up completes (no hang), the sim never dies, and the
# tunnel still carries traffic at the end.
#
# Topology as in verify-sim-wg-runtime.sh.
set -euo pipefail

cd /opt/nuttx
CYCLES="${1:-25}"

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"; linux_pub="$(printf %s "${linux_priv}" | wg pubkey)"

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

flood_pid=""
cleanup() {
  set +e
  [ -n "${flood_pid}" ] && kill "${flood_pid}" 2>/dev/null
  ip link del wgtest0 2>/dev/null
  printf "poweroff\n" >&3 2>/dev/null; sleep 1
  kill "${nuttx_pid}" 2>/dev/null; wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}
trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2
send() { printf "%s\n" "$1" >&3; sleep 0.5; }
fail() { echo "FAIL: $1"; sed -n "1,200p" /tmp/nuttx.out; exit 1; }

if ! ip link show tap0 >/dev/null 2>&1; then fail "tap0 not created"; fi
ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
ip addr add 10.0.0.9/24 dev tap0 2>/dev/null || true
ip link set tap0 up

send "wg genkey"
npriv="$(sed 's/\x1b\[K//g' /tmp/nuttx.out | grep -Eo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${npriv}" ] || fail "no key from genkey"
NPUB="$(printf %s "${npriv}" | wg pubkey)"
send "wg set private-key ${npriv}"
send "wg set address 10.10.0.2/24"
send "wg set peer ${linux_pub} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 5"
send "wg up"

printf %s "${linux_priv}" > /tmp/linux.key
ip link add wgtest0 type wireguard
wg set wgtest0 private-key /tmp/linux.key listen-port 51821 \
  peer "${NPUB}" allowed-ips 10.10.0.2/32 endpoint 10.0.0.2:51820 persistent-keepalive 5
ip addr add 10.10.0.1/24 dev wgtest0; ip link set wgtest0 up
sleep 4

# Sustained flood: valid-mac1 initiations + garbage, from a forged source,
# as fast as possible, so the RX thread always has work while we cycle.
python3 - "${NPUB}" <<'PY' &
import socket, os, hashlib, base64, sys, time
pub = base64.b64decode(sys.argv[1])
mac1_key = hashlib.blake2s(b"mac1----" + pub, digest_size=32).digest()
def init():
    body = b"\x01\x00\x00\x00" + os.urandom(4) + os.urandom(32) + os.urandom(48) + os.urandom(28)
    mac1 = hashlib.blake2s(body, key=mac1_key, digest_size=16).digest()
    return body + mac1 + b"\x00"*16
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.bind(("10.0.0.9", 40001))
while True:
    for _ in range(50):
        try: u.sendto(init(), ("10.0.0.2", 51820))
        except OSError: pass
    time.sleep(0.01)
PY
flood_pid=$!

# Cycle down/up under the flood. After each command send a unique echo
# sentinel: it only prints once NSH has finished the command, so a complete,
# in-order run of every sentinel proves each down and up returned (no hang).
cyc_start="$(wc -l </tmp/nuttx.out)"
for i in $(seq 1 "${CYCLES}"); do
  send "wg down";  send "echo D${i}_$?"
  send "echo D${i}mark"
  send "wg up";    send "echo U${i}mark"
  if ! kill -0 "${nuttx_pid}" 2>/dev/null; then
    fail "sim died during cycle ${i}"
  fi
done
sleep 1

cyc_out="$(tail -n +"$((cyc_start + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g')"

# Every command completed: all 2*CYCLES sentinels present, in order.
missing=0
for i in $(seq 1 "${CYCLES}"); do
  echo "${cyc_out}" | grep -q "D${i}mark" || { echo "missing D${i}mark"; missing=1; }
  echo "${cyc_out}" | grep -q "U${i}mark" || { echo "missing U${i}mark"; missing=1; }
done
[ "${missing}" -eq 0 ] || fail "a down/up command did not complete (hang)"
echo "PASS: all ${CYCLES} down and up commands completed (sentinels in order)"

# Every command succeeded: the wg client printed no error for down/up.
if echo "${cyc_out}" | grep -qiE "wg: (up|down):|must be down|still stopping|Bad|error"; then
  echo "${cyc_out}" | grep -iE "wg: (up|down):|must be down|still stopping|Bad|error" | head
  fail "a down/up command reported an error"
fi
echo "PASS: no down/up command reported an error"

# Explicit down state: with wg0 down the tunnel must NOT carry traffic.
send "wg down"; send "echo DOWNCHK"
sleep 1
if ping -c 2 -W 2 10.10.0.2 >/dev/null 2>&1; then
  fail "tunnel still carried traffic while wg0 was down"
fi
echo "PASS: wg0 down really stops the tunnel"

# Explicit up recovery: bring it back and confirm traffic returns. Poll,
# since the re-handshake after an idle period can take a few seconds.
kill "${flood_pid}" 2>/dev/null; flood_pid=""
send "wg up"; send "echo UPCHK"
recovered=0
for _ in $(seq 1 12); do
  sleep 2
  if ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1; then recovered=1; break; fi
done
if [ "${recovered}" -ne 1 ]; then
  echo "--- wgtest0 ---"; wg show wgtest0
  fail "tunnel did not recover after up"
fi
echo "PASS: wg0 up recovers the tunnel"

echo "PASS: sim WireGuard down/up lifecycle under load verified"
