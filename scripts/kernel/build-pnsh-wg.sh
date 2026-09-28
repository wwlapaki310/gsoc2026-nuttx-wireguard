#!/usr/bin/env bash
# Build the WireGuard device and the wg command under BUILD_PROTECTED with
# real networking, on rv-virt:pnsh64.
#
# Why this board: PROTECTED had only ever been *built* here (mr-canhubk3:knsh),
# never run, and mr-canhubk3 has no emulator. rv-virt:pnsh64 is a first-class
# NuttX PROTECTED config for a target QEMU can run, and rv-virt's virtio-net is
# the same known-good device the BUILD_KERNEL runtime proof uses. So this is the
# one path to PROTECTED *runtime* without new hardware.
#
# Two things have to be adjusted, and both are configuration rather than code:
#
#   - pnsh64 ships with no networking at all, so the stack, virtio-net and the
#     driver are switched on here.
#   - netdev_upperhalf and the TCP timers run on the work queues, and virtio
#     arrives through OpenAMP, whose libmetal calls up_addrenv_va_to_pa
#     unconditionally -- a symbol that only exists on MMU builds.
#
# The stock memory split is left alone: the kernel comes to 205 KB of the
# 256 KB below CONFIG_NUTTX_USERSPACE and the user side to 21 KB of the 256 KB
# above it, so this is stock pnsh64 plus networking and nothing else.
#
# Run inside the wgdev container after scripts/kdev.sh sync.
set -euo pipefail

cd /opt/nuttx

make distclean >/dev/null 2>&1 || true
./tools/configure.sh rv-virt:pnsh64 >/dev/null

# Networking, taken from what rv-virt:knetnsh64 enables for the same hardware.
for o in \
  DRIVERS_VIRTIO DRIVERS_VIRTIO_MMIO DRIVERS_VIRTIO_NET   SCHED_HPWORK SCHED_LPWORK DEV_SIMPLE_ADDRENV \
  NET NET_IPv4 NET_UDP NET_TCP NET_ICMP NET_ICMP_SOCKET NET_SOCKOPTS \
  NET_BROADCAST NETDEV_STATISTICS \
  NETUTILS_IFCONFIG NETUTILS_PING SYSTEM_PING \
  ALLOW_BSD_COMPONENTS \
  DEV_URANDOM CRYPTO CRYPTO_RANDOM_POOL DEV_URANDOM_RANDOM_POOL \
  NET_WIREGUARD SYSTEM_WG
do
  kconfig-tweak --enable "CONFIG_$o" >/dev/null
done

# Two things the netdev path needs that a non-networking config does not:
# netdev_upperhalf and the TCP timers run on the work queues, and virtio comes
# through OpenAMP, whose libmetal calls up_addrenv_va_to_pa unconditionally.
# That symbol only exists with CONFIG_ARCH_ADDRENV, i.e. an MMU build -- this is
# an MPU build, so it comes from DEV_SIMPLE_ADDRENV instead, which with no
# translation table registered is the identity map that VA == PA wants.

# The constant-seed /dev/urandom would hand out the same key every boot.
kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128 >/dev/null

# virtio-net prepends its own header, so the link-layer guard has to cover
# ETH_HDRLEN + VIRTIO_NET_LLHDRSIZE or the driver refuses to compile. These are
# the values rv-virt:knetnsh64 uses for the same device.
kconfig-tweak --set-val CONFIG_NET_LL_GUARDSIZE 32 >/dev/null
kconfig-tweak --set-val CONFIG_NET_ETH_PKTSIZE 1514 >/dev/null
kconfig-tweak --set-val CONFIG_IOB_BUFSIZE 1534 >/dev/null
kconfig-tweak --set-val CONFIG_IOB_THROTTLE 2 >/dev/null

kconfig-tweak --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4 >/dev/null
kconfig-tweak --set-str CONFIG_SYSTEM_WG_CONFIG_PATH /tmp/wg0.conf >/dev/null
# NSH needs a long line for base64 keys.
kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 >/dev/null
kconfig-tweak --set-val CONFIG_LINE_MAX 160 >/dev/null

# pnsh64 boots through nxinit, whose init.rc starts a "console" service by
# exec'ing "sh" along CONFIG_PATH_INITIAL (/system/bin). Nothing in this
# configuration mounts binfs there, so that exec fails with ENOENT and the
# service restarts forever. Start NSH directly as the init task instead, which
# is what every other config on this board does. Nothing about the driver or the
# MPU split depends on which of the two brings up the shell.
kconfig-tweak --disable CONFIG_SYSTEM_NXINIT >/dev/null
kconfig-tweak --set-str CONFIG_INIT_ENTRYPOINT nsh_main >/dev/null

# Nothing here needs the OS test suite or the prime-number benchmark, and they
# cost user-side space the wg command needs.
kconfig-tweak --disable CONFIG_TESTING_OSTEST >/dev/null
kconfig-tweak --disable CONFIG_TESTING_GETPRIME >/dev/null

make olddefconfig >/dev/null 2>&1

echo "=== effective options ==="
grep -E "^CONFIG_(BUILD_PROTECTED|NUTTX_USERSPACE|RAM_SIZE|NET_WIREGUARD|SYSTEM_WG|CRYPTO_CURVE25519|DRIVERS_VIRTIO_NET|IOB_NCHAINS)=" .config

echo "=== building ==="
make -j"$(nproc)" >/tmp/pnsh-build.log 2>&1 && rc=0 || rc=$?
grep -E "error|undefined reference|will not fit|region .* overflowed|warning: .*(wireguard|wg_)" \
  /tmp/pnsh-build.log | grep -v "^ *$" | head -40 || true
echo "PROTECTED_BUILD_EXIT=$rc"
[ "$rc" -eq 0 ] || exit "$rc"

echo "=== image sizes ==="
ls -l nuttx nuttx_user.elf 2>/dev/null | awk '{print $NF, $5}'
for image in nuttx nuttx_user.elf; do
  [ -f "$image" ] || continue
  echo "--- $image"
  riscv-none-elf-size "$image" 2>/dev/null ||
    riscv64-unknown-elf-size "$image" 2>/dev/null ||
    size "$image" 2>/dev/null || true
done
