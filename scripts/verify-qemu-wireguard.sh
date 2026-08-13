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
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP ""
kconfig-tweak --set-val CONFIG_NET_WIREGUARD_PEER_ENDPOINT_PORT 51821
make olddefconfig >/tmp/qemu-wg-olddefconfig.log
make -j"$(nproc)" >/tmp/qemu-wg-build.log

rm -f /tmp/qemu.in /tmp/qemu.out
mkfifo /tmp/qemu.in

ip tuntap add dev tapqemu mode tap
ip addr add 10.0.0.1/24 dev tapqemu
ip link set tapqemu up

qemu-system-arm \
  -M virt \
  -cpu cortex-a7 \
  -nographic \
  -kernel /opt/nuttx/nuttx \
  -netdev tap,id=n0,ifname=tapqemu,script=no,downscript=no \
  -device virtio-net-device,netdev=n0 \
  </tmp/qemu.in >/tmp/qemu.out 2>&1 &
qemu_pid=$!

cleanup() {
  set +e
  ip link del wgtest0 2>/dev/null
  ip link del tapqemu 2>/dev/null
  printf "\001x" >&3 2>/dev/null
  kill "${qemu_pid}" 2>/dev/null
  wait "${qemu_pid}" 2>/dev/null
  rm -f /tmp/qemu.in
}

trap cleanup EXIT

exec 3>/tmp/qemu.in

for _ in $(seq 1 30); do
  if grep -q "nsh>" /tmp/qemu.out; then
    break
  fi
  sleep 1
done

printf "ifconfig\nwg\nifconfig\n" >&3
sleep 3

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

echo "Waiting for QEMU WireGuard handshake..."
sleep 8

set +e
ping -c 3 -W 3 10.10.0.2
ping_status=$?
set -e

printf "wg show\n" >&3
sleep 1

echo "===== Linux wg show ====="
wg show wgtest0 || true

echo "===== QEMU NuttX output ====="
sed -n "1,260p" /tmp/qemu.out

if [ "${ping_status}" -ne 0 ]; then
  echo "ERROR: QEMU WireGuard tunnel ping failed"
  exit "${ping_status}"
fi

echo "PASS: QEMU WireGuard tunnel ping succeeded"
