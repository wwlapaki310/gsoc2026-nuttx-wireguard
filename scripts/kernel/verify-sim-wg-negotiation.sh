#!/usr/bin/env bash
# TN: negative interop against a real Linux kernel WireGuard peer. Things
# that must NOT connect, with a positive control in the middle so a failure
# is known to come from the mismatch and not a broken harness.
#
#  1. Wrong peer public key -> no handshake, no tunnelled traffic.
#  2. (control) the correct key -> handshake and traffic.
#  3. Preshared-key mismatch (NuttX has one, Linux does not) -> no handshake.
#
# Topology as in verify-sim-wg-runtime.sh: NuttX sim on tap0 (10.0.0.2:51820,
# tunnel 10.10.0.2), Linux wgtest0 (10.0.0.1:51821, tunnel 10.10.0.1).
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"; linux_pub="$(printf %s "${linux_priv}" | wg pubkey)"
wrong_pub="$(wg genkey | wg pubkey)"           # a key that is nobody's
echo "Linux public key: ${linux_pub}"

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
nuttx_priv="$(sed 's/\x1b\[K//g' /tmp/nuttx.out | grep -Eo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${nuttx_priv}" ] || fail "no key from genkey"
nuttx_pub="$(printf %s "${nuttx_priv}" | wg pubkey)"
send "wg set private-key ${nuttx_priv}"
send "wg set address 10.10.0.2/24"

# Linux side is correct throughout: it expects the real NuttX key.
printf %s "${linux_priv}" > /tmp/linux.key
ip link add wgtest0 type wireguard
wg set wgtest0 private-key /tmp/linux.key listen-port 51821 \
  peer "${nuttx_pub}" allowed-ips 10.10.0.2/32 endpoint 10.0.0.2:51820 \
  persistent-keepalive 5
ip addr add 10.10.0.1/24 dev wgtest0
ip link set wgtest0 up

handshook() { wg show wgtest0 latest-handshakes | awk '{print $2}' | grep -qv '^0$'; }

# --- 1. wrong key ------------------------------------------------------
send "wg set peer ${wrong_pub} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 5"
send "wg up"
sleep 6
if ping -c 2 -W 2 10.10.0.2 >/dev/null 2>&1 || handshook; then
  fail "wrong peer key still connected"
fi
echo "PASS: wrong peer key did not connect"

# --- 2. control: correct key connects ----------------------------------
send "wg down"
send "wg set peer ${wrong_pub} remove"
send "wg set peer ${linux_pub} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 5"
send "wg up"
sleep 8
if ! ping -c 3 -W 3 10.10.0.2 >/dev/null 2>&1; then
  echo "--- wgtest0 ---"; wg show wgtest0
  fail "correct key did not connect (control)"
fi
echo "PASS: correct key connected (control)"

# --- 3. preshared-key mismatch -----------------------------------------
psk="$(wg genkey)"                              # Linux has no PSK for this peer
send "wg down"
send "wg set peer ${linux_pub} preshared-key ${psk}"
send "wg up"
sleep 8
if ping -c 2 -W 2 10.10.0.2 >/dev/null 2>&1; then
  fail "PSK mismatch still connected"
fi
echo "PASS: preshared-key mismatch did not connect"

echo "PASS: sim WireGuard negative interop verified (TN)"
