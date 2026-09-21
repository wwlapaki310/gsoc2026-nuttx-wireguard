#!/usr/bin/env bash
# T3: two Linux WireGuard peers holding sessions with wg0 at the same time.
#
# NuttX wg0 = 10.10.0.2, on tap0 (10.0.0.2:51820). Two Linux interfaces on
# the same host, different ports and tunnel addresses:
#   wgtest0  10.10.0.1  listen 51821
#   wgtest1  10.10.0.3  listen 51822
# NuttX has one peer per Linux interface, with non-overlapping allowed-ips.
# The test pings each peer's tunnel address from NuttX and confirms both
# Linux interfaces record a handshake and carry bytes -- i.e. two live
# sessions at once.
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

p0="$(wg genkey)"; P0="$(printf %s "${p0}" | wg pubkey)"
p1="$(wg genkey)"; P1="$(printf %s "${p1}" | wg pubkey)"

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  set +e
  ip link del wgtest0 2>/dev/null; ip link del wgtest1 2>/dev/null
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

# NuttX side: one key, two peers with disjoint allowed-ips.
send "wg genkey"
npriv="$(sed 's/\x1b\[K//g' /tmp/nuttx.out | grep -Eo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${npriv}" ] || fail "no key from genkey"
NPUB="$(printf %s "${npriv}" | wg pubkey)"
send "wg set private-key ${npriv}"
send "wg set address 10.10.0.2/24"
send "wg set peer ${P0} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 5"
send "wg set peer ${P1} endpoint 10.0.0.1:51822 allowed-ips 10.10.0.3/32 persistent-keepalive 5"
send "wg up"

# Two Linux interfaces, each a peer of NuttX.
printf %s "${p0}" > /tmp/p0.key; printf %s "${p1}" > /tmp/p1.key
ip link add wgtest0 type wireguard
wg set wgtest0 private-key /tmp/p0.key listen-port 51821 \
  peer "${NPUB}" allowed-ips 10.10.0.2/32 endpoint 10.0.0.2:51820 persistent-keepalive 5
ip addr add 10.10.0.1/24 dev wgtest0; ip link set wgtest0 up

ip link add wgtest1 type wireguard
wg set wgtest1 private-key /tmp/p1.key listen-port 51822 \
  peer "${NPUB}" allowed-ips 10.10.0.2/32 endpoint 10.0.0.2:51820 persistent-keepalive 5
ip addr add 10.10.0.3/24 dev wgtest1; ip link set wgtest1 up

sleep 8

# Ping wg0 (10.10.0.2) through each Linux interface in turn: -I binds the
# source to that interface's tunnel address, so each ping exercises one
# peer's session and NuttX routes the reply back to the matching peer.
ping -c 3 -W 3 -I wgtest0 10.10.0.2 >/dev/null 2>&1 || fail "no traffic through peer 0 (wgtest0)"
echo "PASS: traffic through peer 0"
ping -c 3 -W 3 -I wgtest1 10.10.0.2 >/dev/null 2>&1 || fail "no traffic through peer 1 (wgtest1)"
echo "PASS: traffic through peer 1"

# Both Linux interfaces must show a handshake and non-zero transfer.
hs() { wg show "$1" latest-handshakes | awk '{print $2}' | grep -qv '^0$'; }
rx() { wg show "$1" transfer | awk '{print $2}' | grep -qv '^0$'; }

hs wgtest0 && rx wgtest0 || { echo "--- wgtest0 ---"; wg show wgtest0; fail "peer 0 has no live session"; }
hs wgtest1 && rx wgtest1 || { echo "--- wgtest1 ---"; wg show wgtest1; fail "peer 1 has no live session"; }
echo "PASS: both Linux peers hold a live session with wg0 simultaneously"

echo "PASS: sim WireGuard multi-peer verified (T3)"
