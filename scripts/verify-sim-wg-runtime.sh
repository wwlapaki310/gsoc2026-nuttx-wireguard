#!/usr/bin/env bash
# Verify that a tunnel configured entirely at runtime actually carries
# traffic, against a real Linux kernel WireGuard peer.
#
# verify-sim-wireguard.sh covers the Kconfig path: keys and peer are baked
# into the build. This covers the opposite one - the build carries no keys
# at all, and everything is set through "wg genkey" / "wg set" after boot,
# which is how a device that must not have its private key in its firmware
# image would be used.
#
# It also exercises the parts that only matter once configuration is
# mutable: showconf writing a file, wg down tearing the interface back
# down, setconf reading the file back, and the tunnel coming up again from
# the restored settings.
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"
linux_pub="$(printf "%s" "${linux_priv}" | wg pubkey)"

echo "Linux public key: ${linux_pub}"

# Deliberately build with NO keys: this is what the runtime path has to
# cope with, and it also proves the tunnel that comes up cannot have been
# configured from Kconfig.
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PRIVATE_KEY ""
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
printf "wg up\n" >&3
sleep 1

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

# Rejected input must leave nothing behind. A bad endpoint used to return an
# error only *after* the peer slot had been taken, so "wg up" would then
# bring up a peer the operator had just been told was not accepted.
#
# No valid peer has been set at this point, so any PublicKey in showconf's
# output can only have come from the request that was refused.
printf 'wg set peer %s endpoint garbage\n' "${linux_pub}" >&3
sleep 1
printf 'wg showconf\n' >&3
sleep 1

if grep -q "PublicKey =" /tmp/nuttx.out; then
  echo "ERROR: a rejected 'wg set peer' still staged a peer"
  sed -n "1,200p" /tmp/nuttx.out
  exit 1
fi

echo "PASS: rejected peer settings stage nothing"
printf "wg set peer %s endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 25\n" "${linux_pub}" >&3
sleep 1
printf "wg showconf\n" >&3
sleep 1
printf "wg up\n" >&3
sleep 2

# wg pubkey on the device must agree with the host's wg(8) for the same
# private key, which cross-checks the Curve25519 derivation.
printf "wg pubkey %s\n" "${nuttx_priv}" >&3
sleep 1

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
printf "wg showconf > /tmp/wg0.conf\n" >&3
sleep 1
printf "wg down\n" >&3
sleep 2
printf "ifconfig\n" >&3
sleep 1

set +e
ping -c 2 -W 2 10.10.0.2 >/dev/null 2>&1
ping_down=$?
set -e

if [ "${ping_down}" -eq 0 ]; then
  echo "ERROR: tunnel still answered after wg down"
  exit 1
fi

echo "PASS: wg down stopped the tunnel"

printf "wg setconf /tmp/wg0.conf\n" >&3
sleep 1
printf "wg showconf\n" >&3
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
echo "PASS: sim WireGuard runtime configuration verified"
