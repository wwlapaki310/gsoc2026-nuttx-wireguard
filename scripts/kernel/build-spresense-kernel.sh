#!/usr/bin/env bash
# Build the Spresense image for the IN-KERNEL WireGuard driver, from clean
# nuttx-13.0.1 checkouts. Run inside the dev container (arm-none-eabi-gcc,
# kconfig-frontends, genromfs).
#
# This exists because the first working image was built on a Docker-cached
# tree and could not be reproduced from a fresh clone: the fresh tree hung at
# early cxd56 boot. That was the known CONFIG_RTC_HIRES regression (issue #9),
# and patches/cxd56-rtc-hires-boot.patch is what makes a clean checkout boot.
#
# Usage:
#   build-spresense-kernel.sh --nuttx-patch P --apps-wg DIR [options]
#
#   --nuttx-patch P   git diff of the net-wireguard fork against its base
#                     (git -C <fork> diff <merge-base>..HEAD)
#   --apps-wg DIR     the system-wg fork's system/wg directory
#   --ssid S --pass P bake Wi-Fi credentials into the headless rcS. Omitted
#                     by default: the image then boots to a prompt and takes
#                     no credentials into any artifact.
#   --workdir DIR     build tree (default /opt/wgspr-build)
#   --out FILE        where to copy nuttx.spk (default <workdir>/nuttx.spk)
#   --ref REF         NuttX tag (default nuttx-13.0.1, the one cxd56 boots)
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
nuttx_patch=""; apps_wg=""; ssid=""; pass=""
workdir="/opt/wgspr-build"; out=""; ref="nuttx-13.0.1"

while [ $# -gt 0 ]; do
  case "$1" in
    --nuttx-patch) nuttx_patch="$2"; shift 2 ;;
    --apps-wg)     apps_wg="$2";     shift 2 ;;
    --ssid)        ssid="$2";        shift 2 ;;
    --pass)        pass="$2";        shift 2 ;;
    --workdir)     workdir="$2";     shift 2 ;;
    --out)         out="$2";         shift 2 ;;
    --ref)         ref="$2";         shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

[ -n "${nuttx_patch}" ] && [ -f "${nuttx_patch}" ] ||
  { echo "--nuttx-patch is required and must exist" >&2; exit 2; }
[ -n "${apps_wg}" ] && [ -d "${apps_wg}" ] ||
  { echo "--apps-wg is required and must be a directory" >&2; exit 2; }
[ -n "${out}" ] || out="${workdir}/nuttx.spk"

rtc_patch="${script_dir}/patches/cxd56-rtc-hires-boot.patch"
[ -f "${rtc_patch}" ] || { echo "missing ${rtc_patch}" >&2; exit 2; }

rm -rf "${workdir}"; mkdir -p "${workdir}"; cd "${workdir}"

echo "== clone ${ref} =="
git clone --quiet --depth=1 --branch "${ref}" \
    https://github.com/apache/nuttx.git nuttx
git clone --quiet --depth=1 --branch "${ref}" \
    https://github.com/apache/nuttx-apps.git apps

echo "== apply the in-kernel driver =="
git -C nuttx apply --whitespace=nowarn "${nuttx_patch}"

echo "== apply the cxd56 CONFIG_RTC_HIRES boot fix (#9) =="
# Without this the board hangs right after cxd56_farapiinitialize, before
# NSH, with or without WireGuard. Fail loudly rather than silently skipping:
# a silently-unapplied patch is exactly how this was missed the first time.
git -C nuttx apply --whitespace=nowarn "${rtc_patch}"
grep -q clock_get_sched_ticks nuttx/sched/clock/clock_systime_timespec.c ||
  { echo "RTC_HIRES fix did not take effect" >&2; exit 1; }

echo "== install apps/system/wg =="
cp -r "${apps_wg}" apps/system/wg

echo "== install the headless rcS =="
etc=nuttx/boards/arm/cxd56xx/spresense/src/etc/init.d
mkdir -p "${etc}"
cp "${script_dir}/../../docker/spresense-kernel-etc/init.d/rc.sysinit" "${etc}/"
cp "${script_dir}/../../docker/spresense-kernel-etc/init.d/rcS"        "${etc}/"
if [ -n "${ssid}" ]; then
  sed -i -e "s|@WIFI_SSID@|${ssid}|" -e "s|@WIFI_PASS@|${pass}|" \
         -e 's|^#if 0 /\* WIFI_CREDENTIALS \*/$|#if 1 /* WIFI_CREDENTIALS */|' \
         "${etc}/rcS"
  echo "   (credentials baked in -- do not publish this image)"
else
  echo "   (no credentials: bring Wi-Fi up by hand after boot)"
fi
printf '\nifeq ($(CONFIG_ETC_ROMFS),y)\nRCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS\nendif\n' \
  >> nuttx/boards/arm/cxd56xx/spresense/src/Make.defs

echo "== configure =="
cd nuttx
./tools/configure.sh -a ../apps spresense:wifi >/dev/null

for o in ALLOW_BSD_COMPONENTS DEV_URANDOM CRYPTO CRYPTO_RANDOM_POOL \
         CRYPTO_CURVE25519 NET_WIREGUARD SYSTEM_WG \
         FS_ROMFS ETC_ROMFS BOARDCTL_ROMDISK; do
  kconfig-tweak --enable "CONFIG_$o"
done
kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128
kconfig-tweak --enable  CONFIG_DEV_URANDOM_RANDOM_POOL
kconfig-tweak --set-val CONFIG_IOB_NCHAINS 8
kconfig-tweak --set-str CONFIG_SYSTEM_WG_CONFIG_PATH "/mnt/spif/wg0.conf"
kconfig-tweak --set-val CONFIG_NSH_LINELEN 160
kconfig-tweak --set-val CONFIG_LINE_MAX 160
kconfig-tweak --enable  CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C
kconfig-tweak --disable CONFIG_WL_GS2200M_DISABLE_DHCPC
kconfig-tweak --enable  CONFIG_DEBUG_FEATURES
kconfig-tweak --enable  CONFIG_DEBUG_WIRELESS_ERROR
make olddefconfig >/dev/null

for required in CONFIG_NET_WIREGUARD=y CONFIG_SYSTEM_WG=y CONFIG_ETC_ROMFS=y; do
  grep -q "^${required}$" .config ||
    { echo "configuration lost ${required}" >&2; exit 1; }
done

echo "== build =="
make -j"$(nproc)" >"${workdir}/build.log" 2>&1 ||
  { tail -40 "${workdir}/build.log"; exit 1; }

cp nuttx.spk "${out}"
echo
echo "image:  ${out}"
echo "sha256: $(sha256sum "${out}" | cut -d' ' -f1)"
echo "config: $(sha256sum .config | cut -d' ' -f1)"
echo "nuttx:  $(git -C "${workdir}/nuttx" rev-parse HEAD) (${ref})"
echo "apps:   $(git -C "${workdir}/apps"  rev-parse HEAD) (${ref})"
