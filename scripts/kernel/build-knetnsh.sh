#!/usr/bin/env bash
# Build the WireGuard device and the wg command under BUILD_KERNEL with
# real networking, on rv-virt:knetnsh64 (a first-class NuttX QEMU target).
# Unlike the qemu-armv7a:knsh build, knetnsh64 already has a known-good
# virtio-net device, so this build is used for a full run-time tunnel
# against a real Linux WireGuard peer (see verify-knetnsh-wg.sh).
#
# Run inside the wgdev container after scripts/kdev.sh sync.
set -euo pipefail

cd /opt/nuttx

make distclean >/dev/null 2>&1 || true
./tools/configure.sh rv-virt:knetnsh64 >/dev/null

for o in \
  NET_IPv4 NET_UDP NET_ICMP NET_ICMP_SOCKET NET_SOCKOPTS \
  ALLOW_BSD_COMPONENTS \
  DEV_URANDOM CRYPTO CRYPTO_RANDOM_POOL DEV_URANDOM_RANDOM_POOL \
  NET_WIREGUARD SYSTEM_WG
do
  kconfig-tweak --enable "CONFIG_$o" >/dev/null
done

kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128 >/dev/null
kconfig-tweak --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4 >/dev/null
kconfig-tweak --set-str CONFIG_SYSTEM_WG_CONFIG_PATH /tmp/wg0.conf >/dev/null
# Give NSH a long line for base64 keys.
kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 >/dev/null
kconfig-tweak --set-val CONFIG_LINE_MAX 160 >/dev/null

make olddefconfig >/dev/null 2>&1

echo "=== effective options ==="
grep -E "^CONFIG_(BUILD_KERNEL|NET_WIREGUARD|SYSTEM_WG|CRYPTO_CURVE25519|DRIVERS_VIRTIO_NET)=" .config

echo "=== building kernel ==="
make -j"$(nproc)" >/tmp/knetnsh-build.log 2>&1 && rc=0 || rc=$?
grep -E "error|undefined reference|warning: .*(wireguard|wg_)" /tmp/knetnsh-build.log | grep -v "^ *$" | head -40 || true
echo "KERNEL_BUILD_EXIT=$rc"
[ "$rc" -eq 0 ] || exit "$rc"

echo "=== building apps ELFs (export + import) ==="
make export >/tmp/knetnsh-export.log 2>&1
cd /opt/apps
./tools/mkimport.sh -z -x /opt/nuttx/nuttx-export-*.tar.gz >/tmp/knetnsh-mkimport.log 2>&1
make import >/tmp/knetnsh-import.log 2>&1
echo "=== apps/bin ==="
ls -la /opt/apps/bin/ | grep -E "wg|init|sh" || true
