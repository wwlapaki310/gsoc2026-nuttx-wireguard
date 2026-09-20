#!/usr/bin/env bash
# Verify that a tunnel configured entirely at runtime actually carries
# traffic, against a real Linux kernel WireGuard peer.
#
# Kernel-driver version (drivers/net/wireguard + apps/system/wg): the wg
# command talks to wg0 over ioctls, the tunnel address is set with
# "wg set address", and the private key is recorded in the configuration
# file by "wg set private-key" so that "wg saveconf" can write a loadable
# file (the driver never returns the key). There are no Kconfig keys, so
# the image is used as built.
#
# Covers: genkey on the device, set/up, a rejected peer leaving nothing
# behind, pubkey agreeing with wg(8), traffic, saveconf, down really
# stopping traffic, setconf restoring it.
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"
linux_pub="$(printf "%s" "${linux_priv}" | wg pubkey)"
echo "Linux public key: ${linux_pub}"

conf="$(sed -n 's/^CONFIG_SYSTEM_WG_CONFIG_PATH="\(.*\)"/\1/p' .config)"
conf="${conf:-/tmp/wg0.conf}"

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

# Without a key "wg up" must fail rather than come up in some half state.
printf "rm %s\n" "${conf}" >&3
printf "wg up\n" >&3
sleep 1
if grep -q "is up" /tmp/nuttx.out; then
  echo "ERROR: wg0 came up without a private key"
  exit 1
fi
echo "PASS: wg up without a key is refused"

# Generate the key on the device and read it back out, the way an operator
# would: the private key never exists anywhere but the board.
printf "wg genkey\n" >&3
sleep 1

nuttx_priv="$(grep -Eo '^[A-Za-z0-9+/]{43}=' /tmp/nuttx.out | tail -1)"
if [ -z "${nuttx_priv}" ]; then
  echo "ERROR: could not read a key back from wg genkey"
  sed -n "1,160p" /tmp/nuttx.out
  exit 1
fi

nuttx_pub="$(printf "%s" "${nuttx_priv}" | wg pubkey)"
echo "NuttX private key generated on device; public key: ${nuttx_pub}"

printf "wg set private-key %s\n" "${nuttx_priv}" >&3
sleep 1
printf "wg set address 10.10.0.2/24\n" >&3
sleep 1

# Rejected input must leave nothing behind: no valid peer has been set at
# this point, so any PublicKey in showconf's output can only have come from
# the request that was refused.
printf 'wg set peer %s endpoint garbage\n' "${linux_pub}" >&3
sleep 1
printf 'wg showconf\n' >&3
sleep 1

if grep -q "PublicKey =" /tmp/nuttx.out; then
  echo "ERROR: a rejected 'wg set peer' still created a peer"
  sed -n "1,200p" /tmp/nuttx.out
  exit 1
fi

echo "PASS: rejected peer settings create nothing"
printf "wg set peer %s endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 25\n" "${linux_pub}" >&3
sleep 1
printf "wg up\n" >&3
sleep 2

# wg pubkey on the device must agree with the host's wg(8) for the same
# private key, which cross-checks the Curve25519 derivation.
printf "wg pubkey %s\n" "${nuttx_priv}" >&3
sleep 1
if ! grep -q "^${nuttx_pub}" /tmp/nuttx.out; then
  echo "ERROR: wg pubkey on the device disagrees with wg(8)"
  exit 1
fi
echo "PASS: wg pubkey agrees with wg(8)"

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

echo "Waiting for handshake (runtime-configured)..."
sleep 5

set +e
ping -c 3 -W 3 10.10.0.2
ping1=$?
set -e

if [ "${ping1}" -ne 0 ]; then
  echo "ERROR: tunnel configured at runtime did not carry traffic"
  echo "===== NuttX output ====="
  sed -n "1,240p" /tmp/nuttx.out
  exit "${ping1}"
fi

echo "PASS: runtime-configured tunnel works"

# Now the persistence path: save, tear down, restore, bring back up.
printf "wg saveconf\n" >&3
sleep 1
printf "cat %s\n" "${conf}" >&3
sleep 1
if ! grep -q "^PrivateKey = ${nuttx_priv}" /tmp/nuttx.out; then
  echo "ERROR: saveconf did not write the private key"
  sed -n "1,240p" /tmp/nuttx.out
  exit 1
fi
echo "PASS: saveconf wrote a loadable file"

printf "wg down\n" >&3
sleep 2

set +e
ping -c 2 -W 2 10.10.0.2 >/dev/null 2>&1
ping_down=$?
set -e

if [ "${ping_down}" -eq 0 ]; then
  echo "ERROR: tunnel still answered after wg down"
  exit 1
fi

echo "PASS: wg down stopped the tunnel"

printf "wg setconf\n" >&3
sleep 1
printf "wg up\n" >&3
sleep 2

echo "Waiting for handshake (restored from file)..."
sleep 6

set +e
ping -c 3 -W 3 10.10.0.2
ping2=$?
set -e

printf "wg show\n" >&3
sleep 1

echo "===== Linux wg show ====="
wg show wgtest0 || true

echo "===== NuttX output ====="
sed -n "1,400p" /tmp/nuttx.out

if [ "${ping2}" -ne 0 ]; then
  echo "ERROR: tunnel restored from a saved config did not carry traffic"
  exit "${ping2}"
fi

echo "PASS: configuration survived a down/up cycle through a file"
echo "PASS: sim WireGuard runtime configuration verified (kernel driver)"
