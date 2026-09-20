#!/usr/bin/env bash
# Build the WireGuard device and the wg command under BUILD_KERNEL
# (qemu-armv7a:knsh). This is the migration's primary proof: the driver
# lives in the kernel and the wg command is a separate ELF, so the two
# must compile and link with the apps/kernel symbol boundary enforced.
#
# It does NOT need a physical netdev to build — the driver's UDP socket is
# a kernel socket, not a virtio-net device. Runtime networking is a
# separate exercise (rv-virt:knetnsh64).
#
# Run inside the wgdev container after scripts/kdev.sh sync.
set -euo pipefail

cd /opt/nuttx

make distclean >/dev/null 2>&1 || true
./tools/configure.sh qemu-armv7a:knsh >/dev/null

for o in \
  NET NET_IPv4 NET_UDP NET_TCP NET_ICMP NET_ICMP_SOCKET \
  ALLOW_BSD_COMPONENTS NET_SOCKOPTS \
  DEV_URANDOM CRYPTO CRYPTO_RANDOM_POOL DEV_URANDOM_RANDOM_POOL \
  NET_WIREGUARD SYSTEM_WG \
  DEVICE_TREE LIBC_FDT DEV_SIMPLE_ADDRENV \
  DRIVERS_VIRTIO DRIVERS_VIRTIO_MMIO DRIVERS_VIRTIO_NET
do
  kconfig-tweak --enable "CONFIG_$o" >/dev/null
done

kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128 >/dev/null
# virtio-net raises the minimum L2 guard; knsh defaults below it, so set a
# floor that clears ETH_HDRLEN + VIRTIO_NET_LLHDRSIZE.
kconfig-tweak --set-val CONFIG_NET_LL_GUARDSIZE 32 >/dev/null
kconfig-tweak --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4 >/dev/null
kconfig-tweak --set-str CONFIG_SYSTEM_WG_CONFIG_PATH /tmp/wg0.conf >/dev/null

make olddefconfig >/dev/null 2>&1

echo "=== effective WireGuard / kernel-build options ==="
grep -E "^CONFIG_(BUILD_KERNEL|NET_WIREGUARD|SYSTEM_WG|CRYPTO_CURVE25519|NET_LL_GUARDSIZE)=" .config

echo "=== building ==="
make -j"$(nproc)" >/tmp/knsh-build.log 2>&1 && rc=0 || rc=$?
grep -E "error|undefined reference|warning: .*(wireguard|wg_)" /tmp/knsh-build.log | grep -v "^ *$" | head -40 || true
echo "KNSH_BUILD_EXIT=$rc"
ls -la nuttx 2>/dev/null | cut -c1-60
# The wg command is a separate ELF under BUILD_KERNEL.
echo "=== wg ELF ==="
find . -path ./nuttx -prune -o -name "wg" -type f -print 2>/dev/null | head
ls -la /opt/apps/bin/wg 2>/dev/null || true
