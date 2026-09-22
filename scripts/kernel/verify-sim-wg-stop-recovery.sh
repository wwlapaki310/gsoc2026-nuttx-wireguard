#!/usr/bin/env bash
# Deliberately time out the ifdown stop and confirm the stopping state is
# recovered. Requires a build with CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL=y,
# which makes the first RX thread sleep past the stop timeout once.
# With argument "output", use CONFIG_NET_WIREGUARD_DEBUG_TX_STALL=y and
# CONFIG_SYSTEM_PING=y instead (leave DEBUG_STOP_STALL disabled).
#
#   up            -> tunnel works (thread 1)
#   down          -> thread 1 stalls; the stop wait times out; ifdown reports
#                    the failure and leaves the interface "stopping"
#   down          -> reaps the now-exited thread 1
#   up            -> comes back up (thread 2)
#   ping          -> the tunnel is carried again
#
# Pass = down reports the timeout, repeated down and then up succeed, the tunnel
# recovers, and the sim never dies.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-stop}"
case "${mode}" in
  stop) option=CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL ;;
  output) option=CONFIG_NET_WIREGUARD_DEBUG_TX_STALL ;;
  *) echo "usage: $0 [stop|output]"; exit 2 ;;
esac
cd /opt/nuttx

if ! grep -q "^${option}=y" .config; then
  echo "SKIP: build without ${option}=y"
  exit 77
fi

if [ "${mode}" = output ] && ! grep -q '^CONFIG_SYSTEM_PING=y' .config; then
  echo "SKIP: output mode requires CONFIG_SYSTEM_PING=y"
  exit 77
fi

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"; linux_pub="$(printf %s "${linux_priv}" | wg pubkey)"

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!
ping_pid=""

cleanup() {
  set +e
  [ -n "${ping_pid}" ] && kill "${ping_pid}" 2>/dev/null
  [ -n "${ping_pid}" ] && wait "${ping_pid}" 2>/dev/null
  ip link del wgtest0 2>/dev/null
  printf "poweroff\n" >&3 2>/dev/null; sleep 1
  kill "${nuttx_pid}" 2>/dev/null; wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}
trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2
send() { printf "%s\n" "$1" >&3; sleep 0.6; }
fail() { echo "FAIL: $1"; sed -n "1,200p" /tmp/nuttx.out; exit 1; }

diagnostics() {
  sed 's/\x1b\[[0-9;]*[[:alpha:]]//g; s/\r//g; s/^nsh> //' /tmp/nuttx.out
}

tags=()
checked() {
  local tag="WGCHK_$1" command="$2" expected="${3:-0}" rc
  tags+=("${tag}=${expected}")
  send "${command}"
  send "echo ${tag}:\$?"
  for _ in $(seq 1 60); do
    rc=0
    python3 "${script_dir}/nsh-status.py" /tmp/nuttx.out "${tags[@]}" || rc=$?
    [ "${rc}" -eq 0 ] && return 0
    [ "${rc}" -eq 1 ] && fail "unexpected status/order for ${command}"
    sleep 0.2
  done
  fail "no completed result for ${command}"
}

await_marker() {
  for _ in $(seq 1 300); do
    grep -qF "$1" /tmp/nuttx.out && return 0
    kill -0 "${nuttx_pid}" 2>/dev/null || fail "sim exited"
    sleep 0.2
  done
  fail "missing marker: $1"
}

if ! ip link show tap0 >/dev/null 2>&1; then fail "tap0 not created"; fi
ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
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

# Wait (poll) for the initial handshake; the sim's first handshake is
# occasionally slow, so don't rely on a single fixed sleep.
poll_ping() {
  local i
  for i in $(seq 1 "${2:-10}"); do
    sleep 2
    ping -c 1 -W 2 "$1" >/dev/null 2>&1 && return 0
  done
  return 1
}
if [ "${mode}" = output ]; then
  # The first non-empty transport datagram is held by the socket worker.
  ping -c 60 -i 0.2 -W 1 10.10.0.2 >/tmp/stall-ping.log 2>&1 &
  ping_pid=$!
  await_marker "wg test: output stall entered"
  checked SHOW "wg show"
  checked UPDATE "wg set peer ${linux_pub} persistent-keepalive 10"
  # Drive upper-half TX while the worker retains a different ciphertext.
  send "ping -c 8 -i 100 -W 100 10.10.0.1 &"
  sleep 3
  if grep -qF "wg test: output stall left" /tmp/nuttx.out; then
    fail "ioctl/TX checks missed the stalled-send window"
  fi
  echo "PASS: query/update completed while output was stalled"
else
  poll_ping 10.10.0.2 10 || fail "tunnel did not come up initially"
  echo "PASS: tunnel up before the stall"
fi

if [ "${mode}" = stop ]; then
  # The first caller releases d_lock while waiting. A second down must
  # return EBUSY, not become another consumer of rxdone.
  send "wg down &"
  checked DBUSY "wg down" 1
  if ! diagnostics | grep -qxE "wg: down: (Unknown error 16|Device or resource busy)"; then
    fail "concurrent down did not return EBUSY"
  fi
  for _ in $(seq 1 60); do
    diagnostics | grep -qxE "wg: down: (Unknown error 110|Connection timed out)" && break
    sleep 0.2
  done
  echo "PASS: concurrent down was excluded from the reap wait"
else
  checked DSTOP "wg down" 1
fi
if ! diagnostics | grep -qxE "wg: down: (Unknown error 110|Connection timed out)"; then
  fail "down failed for a reason other than ETIMEDOUT"
fi
kill -0 "${nuttx_pid}" 2>/dev/null || fail "sim died on timed-out down"
echo "PASS: down reported ETIMEDOUT with the worker still owned"
checked KEYBUSY "wg set private-key ${npriv}" 1
checked UPBUSY "wg up" 1

if [ "${mode}" = output ]; then
  await_marker "wg test: output stall left"
  grep -qF "wg test: output owned 4" /tmp/nuttx.out || fail "TX queue was not saturated"
  echo "PASS: retained ciphertext passed the driver integrity assertion"
else
  sleep 3
fi

checked REAP "wg down"
checked UP "wg up"
if ! poll_ping 10.10.0.2 12; then
  fail "tunnel did not recover after the stopping state"
fi
echo "PASS: repeated down reaped the worker; up restored traffic"
echo "PASS: sim WireGuard ${mode}-stall recovery verified"
