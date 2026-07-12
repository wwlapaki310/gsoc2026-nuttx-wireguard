#!/usr/bin/env bash
# =============================================================================
# NuttX WireGuard build script
#
# Usage:
#   scripts/build.sh <sim|qemu|esp32|spresense> [--clean]
#
# Environment:
#   NUTTX_WS   workspace directory (default: ~/nuttx-ws)
#              layout: $NUTTX_WS/nuttx, $NUTTX_WS/apps, $NUTTX_WS/wireguard-lwip
#   NUTTX_REF  NuttX git tag (default: nuttx-12.7.0)
#
# Requirements (Ubuntu 24.04):
#   apt: kconfig-frontends gcc-arm-none-eabi binutils-arm-none-eabi
#        libnewlib-arm-none-eabi genromfs xxd
#   pip: kconfiglib esptool
#   ESP32: xtensa-esp32-elf toolchain in PATH or at
#          $NUTTX_WS/toolchains/xtensa-esp32-elf
# =============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NUTTX_WS="${NUTTX_WS:-$HOME/nuttx-ws}"
NUTTX_REF="${NUTTX_REF:-nuttx-12.7.0}"

TARGET="${1:-}"
CLEAN="${2:-}"

usage() {
  echo "Usage: $0 <sim|qemu|esp32|spresense> [--clean]"
  exit 1
}

[ -n "$TARGET" ] || usage

# -----------------------------------------------------------------------------
# 1. Fetch sources
# -----------------------------------------------------------------------------
mkdir -p "$NUTTX_WS"

if [ ! -d "$NUTTX_WS/nuttx" ]; then
  git clone --depth=1 --branch "$NUTTX_REF" \
    https://github.com/apache/nuttx.git "$NUTTX_WS/nuttx"
fi

if [ ! -d "$NUTTX_WS/apps" ]; then
  git clone --depth=1 --branch "$NUTTX_REF" \
    https://github.com/apache/nuttx-apps.git "$NUTTX_WS/apps"
fi

if [ ! -d "$NUTTX_WS/wireguard-lwip" ]; then
  git clone --depth=1 \
    https://github.com/smartalock/wireguard-lwip.git "$NUTTX_WS/wireguard-lwip"
fi

# -----------------------------------------------------------------------------
# 2. Install WireGuard sources into apps/netutils/wireguard
#    - protocol core + crypto from wireguard-lwip
#    - NuttX port files from this repository (overwrites shims/build files)
#    - upstream wireguardif.[ch] are lwIP-only and are NOT used
# -----------------------------------------------------------------------------
WG_DIR="$NUTTX_WS/apps/netutils/wireguard"
rm -rf "$WG_DIR"
mkdir -p "$WG_DIR"
cp -r "$NUTTX_WS/wireguard-lwip/src/." "$WG_DIR/"
rm -f "$WG_DIR/wireguardif.c" "$WG_DIR/wireguardif.h"
cp -r "$REPO_ROOT/nuttx_port/apps/netutils/wireguard/." "$WG_DIR/"

# Regenerate the netutils Kconfig menu so wireguard appears
(cd "$NUTTX_WS/apps/netutils" && \
  bash "$NUTTX_WS/apps/tools/mkkconfig.sh" -m "Network Utilities" -o Kconfig)

# -----------------------------------------------------------------------------
# 3. Configure the board
# -----------------------------------------------------------------------------
cd "$NUTTX_WS/nuttx"

case "$TARGET" in
  sim)       BOARD_CONFIG="sim:nsh" ;;
  qemu)      BOARD_CONFIG="qemu-armv7a:nsh" ;;
  esp32)     BOARD_CONFIG="esp32-devkitc:wifi" ;;
  spresense) BOARD_CONFIG="spresense:rndis" ;;
  *)         usage ;;
esac

MARKER=".wireguard-target"
if [ "$CLEAN" = "--clean" ] || [ ! -f .config ] || \
   [ "$(cat $MARKER 2>/dev/null)" != "$TARGET" ]; then
  make distclean >/dev/null 2>&1 || true
  ./tools/configure.sh "$BOARD_CONFIG"   # apps/ auto-detected as ../apps
  echo "$TARGET" > "$MARKER"
fi

# Common WireGuard configuration
kconfig-tweak --enable  CONFIG_NET
kconfig-tweak --enable  CONFIG_NET_IPv4
kconfig-tweak --enable  CONFIG_NET_UDP
kconfig-tweak --enable  CONFIG_ALLOW_BSD_COMPONENTS
kconfig-tweak --enable  CONFIG_NET_TUN
kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500
kconfig-tweak --enable  CONFIG_DEV_URANDOM
kconfig-tweak --enable  CONFIG_NET_WIREGUARD
kconfig-tweak --enable  CONFIG_NET_ICMP
kconfig-tweak --enable  CONFIG_NET_ICMP_SOCKET
kconfig-tweak --enable  CONFIG_SYSTEM_PING

# base64 keys make wg commands long - raise the NSH line limit
kconfig-tweak --set-val CONFIG_NSH_LINELEN 160

# iperf (argtable3) and wapi initconf (cJSON) download source tarballs at
# build time and are not needed for WireGuard - disable everywhere.
kconfig-tweak --disable CONFIG_NETUTILS_IPERF
kconfig-tweak --disable CONFIG_SYSTEM_ARGTABLE3
kconfig-tweak --disable CONFIG_WIRELESS_WAPI_INITCONF
kconfig-tweak --disable CONFIG_NETUTILS_CJSON

# Per-target extras
case "$TARGET" in
  sim)
    kconfig-tweak --enable CONFIG_NET_TCP
    kconfig-tweak --enable CONFIG_SIM_NETDEV
    ;;
  qemu)
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO_MMIO
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO_NET
    kconfig-tweak --enable CONFIG_NETDEV_LATEINIT
    # virtio-net needs room for its link layer header in front of the
    # ethernet header
    kconfig-tweak --set-val CONFIG_NET_LL_GUARDSIZE 32
    # libmetal's NuttX backend calls up_addrenv_{pa_to_va,va_to_pa};
    # DEV_SIMPLE_ADDRENV provides the identity-mapped implementation
    kconfig-tweak --enable CONFIG_DEV_SIMPLE_ADDRENV
    # the board discovers virtio-mmio devices from the QEMU device tree
    kconfig-tweak --enable CONFIG_DEVICE_TREE
    kconfig-tweak --enable CONFIG_LIBC_FDT

    # libfdt comes from dgibson/dtc; a git clone skips the zip download
    DTC_VERSION="$(sed -n 's/^CONFIG_LIBC_FDT_DTC_VERSION="\(.*\)"/\1/p' .config)"
    DTC_VERSION="${DTC_VERSION:-1.7.0}"
    if [ ! -d libs/libc/fdt/dtc/.git ]; then
      rm -rf libs/libc/fdt/dtc
      git clone --depth=1 --branch "v$DTC_VERSION" \
        https://github.com/dgibson/dtc.git libs/libc/fdt/dtc
    fi

    # virtio pulls in OpenAMP; provide libmetal/open-amp as git clones so
    # the build skips its zip download (works behind restricted proxies).
    # The zip flow would apply the NuttX patch series - replay it here.
    OPENAMP_VERSION="$(sed -n 's/^VERSION ?= //p' openamp/Makefile)"
    for dep in libmetal open-amp; do
      if [ ! -d "openamp/$dep/.git" ]; then
        rm -rf "openamp/$dep"
        git clone --depth=1 --branch "v$OPENAMP_VERSION" \
          "https://github.com/OpenAMP/$dep.git" "openamp/$dep"
        (cd openamp && \
         sed -n 's/^.*patch -p0 < \(.*\)$/\1/p' "$dep.defs" | \
         while read -r p; do patch -p0 < "$p"; done)
      fi
    done
    ;;
  esp32)
    # esp32-devkitc:wifi already enables the Wi-Fi station + network stack
    ;;
  spresense)
    # spresense:rndis already enables USB RNDIS networking
    ;;
esac

make olddefconfig

# -----------------------------------------------------------------------------
# 4. Build
# -----------------------------------------------------------------------------
if [ "$TARGET" = "esp32" ] && ! command -v xtensa-esp32-elf-gcc >/dev/null; then
  export PATH="$NUTTX_WS/toolchains/xtensa-esp32-elf/bin:$PATH"
fi

make -j"$(nproc)"

# -----------------------------------------------------------------------------
# 5. Collect artifacts
# -----------------------------------------------------------------------------
OUT="$NUTTX_WS/out/$TARGET"
mkdir -p "$OUT"
cp -f nuttx "$OUT/" 2>/dev/null || true
case "$TARGET" in
  esp32)     cp -f nuttx.bin "$OUT/" ;;
  spresense) cp -f nuttx.spk "$OUT/" ;;
esac

echo
echo "=== Build artifacts ($TARGET) -> $OUT ==="
ls -lh "$OUT"
