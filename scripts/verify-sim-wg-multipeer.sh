#!/usr/bin/env bash
# Verify that two peers can hold sessions with wg0 at the same time,
# against two real Linux kernel WireGuard interfaces.
#
# The multi-peer work was otherwise only checked by reading "wg show" and
# "wg showconf", which proves the configuration structures are populated
# but not that two sessions can actually coexist - separate key pairs,
# separate replay counters, and allowed-ips picking the right peer for a
# reply are all things that only a second live peer exercises.
#
# Requires CONFIG_NET_WIREGUARD_MAX_PEERS >= 2 (the sim stage sets 4).
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

max_peers="$(grep -E '^CONFIG_NET_WIREGUARD_MAX_PEERS=' .config | cut -d= -f2)"
if [ "${max_peers:-1}" -lt 2 ]; then
  echo "SKIP: CONFIG_NET_WIREGUARD_MAX_PEERS is ${max_peers}, need >= 2"
  exit 0
fi

nuttx_priv="$(wg genkey)"
nuttx_pub="$(printf "%s" "${nuttx_priv}" | wg pubkey)"
a_priv="$(wg genkey)"; a_pub="$(printf "%s" "${a_priv}" | wg pubkey)"
b_priv="$(wg genkey)"; b_pub="$(printf "%s" "${b_priv}" | wg pubkey)"

echo "NuttX  public key: ${nuttx_pub}"
echo "peer A public key: ${a_pub}"
echo "peer B public key: ${b_pub}"

# Configure the interface key through Kconfig but both peers at runtime:
# there is only one CONFIG_NET_WIREGUARD_PEER_* set, so a second peer can
# only come from "wg set peer" - which is the point being tested.
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PRIVATE_KEY "${nuttx_priv}"
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PEER_PUBLIC_KEY ""
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP ""
make olddefconfig >/tmp/wg-olddefconfig.log
make -j"$(nproc)" >/tmp/wg-build.log

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in

/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  set +e
  ip link del wgtest0 2>/dev/null
  ip link del wgtest1 2>/dev/null
  printf "poweroff\n" >&3 2>/dev/null
  sleep 1
  kill "${nuttx_pid}" 2>/dev/null
  wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}

trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2

if ip link show tap0 >/dev/null 2>&1; then
  ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
  ip link set tap0 up
else
  echo "ERROR: tap0 was not created by NuttX sim"
  sed -n "1,160p" /tmp/nuttx.out
  exit 1
fi

# Two peers, each routed by its own allowed-ips inside the same /24 that
# wg0 sits on, so replies pick a peer by destination rather than by luck.
printf "wg set peer %s endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 25\n" "${a_pub}" >&3
sleep 1
printf "wg set peer %s endpoint 10.0.0.1:51822 allowed-ips 10.10.0.3/32 persistent-keepalive 25\n" "${b_pub}" >&3
sleep 1
printf "wg showconf\n" >&3
sleep 1
printf "wg up\n" >&3
sleep 2

printf "%s\n" "${a_priv}" > /tmp/a.key
printf "%s\n" "${b_priv}" > /tmp/b.key

# /32 addresses so neither interface installs a subnet route; the route to
# wg0 is added explicitly on A only, leaving B as a handshake-only peer.
ip link add wgtest0 type wireguard
wg set wgtest0 private-key /tmp/a.key listen-port 51821 \
  peer "${nuttx_pub}" allowed-ips 10.10.0.2/32 \
  endpoint 10.0.0.2:51820 persistent-keepalive 25
ip addr add 10.10.0.1/32 dev wgtest0
ip link set wgtest0 up
ip route add 10.10.0.2/32 dev wgtest0

ip link add wgtest1 type wireguard
wg set wgtest1 private-key /tmp/b.key listen-port 51822 \
  peer "${nuttx_pub}" allowed-ips 10.10.0.2/32 \
  endpoint 10.0.0.2:51820 persistent-keepalive 25
ip addr add 10.10.0.3/32 dev wgtest1
ip link set wgtest1 up

echo "Waiting for both handshakes..."
sleep 8

set +e
ping -c 3 -W 3 10.10.0.2
ping_status=$?
set -e

printf "wg show\n" >&3
sleep 1

echo "===== Linux wg show (both interfaces) ====="
wg show wgtest0 || true
echo
wg show wgtest1 || true

echo "===== NuttX output ====="
sed -n "1,400p" /tmp/nuttx.out

# Both Linux ends must report a handshake: that is what distinguishes two
# live sessions from one session and one peer that merely exists.
a_hs="$(wg show wgtest0 latest-handshakes | awk '{print $2}')"
b_hs="$(wg show wgtest1 latest-handshakes | awk '{print $2}')"

echo "peer A latest-handshake epoch: ${a_hs:-none}"
echo "peer B latest-handshake epoch: ${b_hs:-none}"

if [ "${a_hs:-0}" -eq 0 ]; then
  echo "ERROR: peer A never completed a handshake"
  exit 1
fi

if [ "${b_hs:-0}" -eq 0 ]; then
  echo "ERROR: peer B never completed a handshake"
  exit 1
fi

echo "PASS: two peers hold sessions simultaneously"

if [ "${ping_status}" -ne 0 ]; then
  echo "ERROR: tunnel ping through peer A failed"
  exit "${ping_status}"
fi

echo "PASS: traffic flows over the multi-peer tunnel"
echo "PASS: sim WireGuard multi-peer verified"
