#!/usr/bin/env bash
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

nuttx_priv="$(wg genkey)"
nuttx_pub="$(printf "%s" "${nuttx_priv}" | wg pubkey)"
linux_priv="$(wg genkey)"
linux_pub="$(printf "%s" "${linux_priv}" | wg pubkey)"

echo "NuttX public key: ${nuttx_pub}"
echo "Linux public key: ${linux_pub}"

kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PRIVATE_KEY "${nuttx_priv}"
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PEER_PUBLIC_KEY "${linux_pub}"
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP "10.0.0.1"
kconfig-tweak --set-val CONFIG_NET_WIREGUARD_PEER_ENDPOINT_PORT 51820
make olddefconfig >/tmp/wg-olddefconfig.log
make -j"$(nproc)" >/tmp/wg-build.log

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

if ip link show tap0 >/dev/null 2>&1; then
  ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
  ip link set tap0 up
else
  echo "ERROR: tap0 was not created by NuttX sim"
  sed -n "1,160p" /tmp/nuttx.out
  exit 1
fi

printf "ifconfig\nwg\nifconfig\n" >&3
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

set +e
ping -c 3 -W 3 10.10.0.2
ping_status=$?
set -e

printf "wg show\n" >&3
sleep 1

echo "===== Linux wg show ====="
wg show wgtest0 || true

echo "===== NuttX output ====="
sed -n "1,240p" /tmp/nuttx.out

if [ "${ping_status}" -ne 0 ]; then
  echo "ERROR: tunnel ping failed"
  exit "${ping_status}"
fi

echo "PASS: sim WireGuard tunnel ping succeeded"
