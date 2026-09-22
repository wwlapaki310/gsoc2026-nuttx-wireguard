#!/usr/bin/env bash
# Lifecycle stress for device locking and queued UDP output:
# repeatedly bring wg0 down and up while a sustained inbound flood hits the
# listen port and a real Linux peer keeps sending. This is the reproduction
# case for
#   - ifdown closing the socket while the RX thread is still draining a
#     flood (the thread now re-checks running in its inner loop and ifdown
#     waits for it to leave before closing), and
#   - TX/RX protocol-state races (all use d_lock; the socket worker sends
#     immutable copies outside the lock).
#
# Pass = every command completes successfully, in order, and each cycle
# stops and restores tunnel traffic. This does not emulate usrsock stalls.
#
# Topology as in verify-sim-wg-runtime.sh.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
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
  [ -n "${flood_pid}" ] && kill -CONT "${flood_pid}" 2>/dev/null
  [ -n "${flood_pid}" ] && kill "${flood_pid}" 2>/dev/null
  [ -n "${flood_pid}" ] && wait "${flood_pid}" 2>/dev/null
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

tags=()
checked() {
  local tag="WGCHK_$1" command="$2" rc=2
  tags+=("${tag}")
  send "${command}"
  # Expand $? on the target, not in this host shell.
  send "echo ${tag}:\$?"
  for _ in $(seq 1 60); do
    rc=0
    python3 "${script_dir}/nsh-status.py" /tmp/nuttx.out "${tags[@]}" || rc=$?
    [ "${rc}" -eq 0 ] && return 0
    [ "${rc}" -eq 1 ] && fail "${command}: failed, duplicate or out-of-order result"
    kill -0 "${nuttx_pid}" 2>/dev/null || fail "sim died during ${command}"
    sleep 0.2
  done
  fail "${command}: no completed result line"
}

await_tunnel() {
  for _ in $(seq 1 20); do
    if ping -c 1 -W 1 10.10.0.2 >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  fail "tunnel did not recover"
}

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
await_tunnel

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

# Check each target-side status, then the actual tunnel state.
for i in $(seq 1 "${CYCLES}"); do
  checked "D${i}" "wg down"
  if ping -c 1 -W 1 10.10.0.2 >/dev/null 2>&1; then
    fail "cycle ${i}: traffic passed while down"
  fi
  checked "U${i}" "wg up"
  # Lifecycle completion is measured under load. Pause the unauthenticated
  # flood for the recovery probe: availability during a sustained DoS is a
  # different property from a correct down/up transition.
  kill -STOP "${flood_pid}"
  await_tunnel
  kill -CONT "${flood_pid}"
  echo "PASS: cycle ${i}: down stopped traffic, up restored traffic"
done
echo "PASS: all ${CYCLES} down/up pairs returned success in order"
kill "${flood_pid}" 2>/dev/null; flood_pid=""
checked "DFINAL" "wg down"
if ping -c 1 -W 1 10.10.0.2 >/dev/null 2>&1; then
  fail "traffic passed while down"
fi
checked "UFINAL" "wg up"
await_tunnel
echo "PASS: sim WireGuard down/up lifecycle under load verified"
