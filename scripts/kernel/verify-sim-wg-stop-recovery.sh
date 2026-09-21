#!/usr/bin/env bash
# Deliberately time out the ifdown stop and confirm the stopping state is
# recovered. Requires a build with CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL=y,
# which makes the first RX thread sleep past the stop timeout once.
#
#   up            -> tunnel works (thread 1)
#   down          -> thread 1 stalls; the stop wait times out; ifdown reports
#                    the failure and leaves the interface "stopping"
#   up            -> reaps the now-exited thread 1 and comes back up (thread 2)
#   ping          -> the tunnel is carried again
#
# Pass = the down reports the timeout, the later up succeeds, the tunnel
# recovers, and the sim never dies.
set -euo pipefail

cd /opt/nuttx

if ! grep -q "^CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL=y" .config; then
  echo "SKIP: build without CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL=y"
  exit 0
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

cleanup() {
  set +e
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
poll_ping 10.10.0.2 10 || fail "tunnel did not come up initially"
echo "PASS: tunnel up before the stall"

# down: the first RX thread stalls past the stop timeout, so d_ifdown returns
# an error. netdev keeps IFF_UP set and the wg client prints an error -- the
# socket is NOT closed from under the live thread.
before="$(wc -l </tmp/nuttx.out)"
send "wg down"
sleep 6                                   # let ifdown's bounded wait time out
if ! tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g' | grep -qi "wg: down:"; then
  fail "ifdown did not report the stop timeout"
fi
if ! kill -0 "${nuttx_pid}" 2>/dev/null; then fail "sim died on timed-out down"; fi
echo "PASS: ifdown reported the stop timeout (interface left stopping, IFF_UP set)"

# Let the stalled thread finish and exit. Recovery is a repeated down: with
# IFF_UP still set, netdev calls d_ifdown again, which reaps the now-exited
# thread and completes teardown, clearing IFF_UP.
sleep 6
before="$(wc -l </tmp/nuttx.out)"
send "wg down"
sleep 2
if tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g' | grep -qi "wg: down:"; then
  fail "repeated down did not reap the stopping interface"
fi
echo "PASS: repeated down reaped the stalled thread (interface fully down)"

# Now a normal up must succeed and the tunnel must come back.
before="$(wc -l </tmp/nuttx.out)"
send "wg up"
sleep 1
if tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g' | grep -qi "wg: up:"; then
  fail "up failed after recovery"
fi
if ! poll_ping 10.10.0.2 12; then
  echo "--- wgtest0 ---"; wg show wgtest0
  fail "tunnel did not recover after the stopping state"
fi
echo "PASS: tunnel recovered after stop timeout"

echo "PASS: sim WireGuard stop-timeout recovery verified"
