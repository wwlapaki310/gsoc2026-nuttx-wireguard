#!/usr/bin/env bash
# T8: the CMake build path.
#
# Everything else here is built with the Makefile path. NuttX's CI also builds
# with CMake, so a driver that only compiles one way fails review for a reason
# that has nothing to do with the driver. drivers/net/CMakeLists.txt does not
# name the wireguard directory explicitly -- it calls nuttx_add_subdirectory(),
# which globs */CMakeLists.txt -- so this checks that the wiring actually works
# rather than assuming it from reading the glob.
#
# Usage: build-cmake-wg.sh [board:config]   default sim:nsh
#
# sim:nsh because the netnsh configs do not build under CMake in this tree at
# all: rv-virt:netnsh64 and rv-virt:netnsh both stop at
#   ninja: error: 'libm.a', needed by 'nuttx', missing and no known rule to make it
# with no WireGuard involved (they set CONFIG_LIBM_TOOLCHAIN). Verified by
# configuring and building them stock -- same failure. sim:nsh and
# rv-virt:nsh64 build clean, so the CMake path itself is fine and that is a
# separate upstream problem, not this driver's.
#
# Run inside the wgdev container after scripts/kdev.sh sync.
set -euo pipefail

cd /opt/nuttx
TARGET="${1:-sim:nsh}"
BUILD=/tmp/cmake-wg

command -v cmake >/dev/null || { echo "FAIL: cmake is not installed"; exit 1; }
command -v ninja >/dev/null || { echo "FAIL: ninja is not installed"; exit 1; }

# CMake refuses to configure over a Makefile build's leftovers.
make distclean >/dev/null 2>&1 || true

rm -rf "${BUILD}"
cmake -B "${BUILD}" -DBOARD_CONFIG="${TARGET}" -GNinja . >/tmp/cmake-configure.log 2>&1 ||
  { echo "FAIL: cmake configure"; tail -30 /tmp/cmake-configure.log; exit 1; }

for o in \
  NET NET_IPv4 NET_UDP NET_TCP NET_ICMP NET_ICMP_SOCKET NET_SOCKOPTS \
  SIM_NETDEV NETUTILS_IFCONFIG \
  ALLOW_BSD_COMPONENTS \
  DEV_URANDOM CRYPTO CRYPTO_RANDOM_POOL DEV_URANDOM_RANDOM_POOL \
  NET_WIREGUARD SYSTEM_WG
do
  kconfig-tweak --file "${BUILD}/.config" --enable "CONFIG_$o" >/dev/null
done
kconfig-tweak --file "${BUILD}/.config" \
  --disable CONFIG_DEV_URANDOM_XORSHIFT128 >/dev/null
kconfig-tweak --file "${BUILD}/.config" \
  --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4 >/dev/null
kconfig-tweak --file "${BUILD}/.config" \
  --set-str CONFIG_SYSTEM_WG_CONFIG_PATH /tmp/wg0.conf >/dev/null

cmake --build "${BUILD}" -t olddefconfig >/tmp/cmake-olddefconfig.log 2>&1 ||
  { echo "FAIL: olddefconfig"; tail -30 /tmp/cmake-olddefconfig.log; exit 1; }

echo "=== effective options ==="
grep -E "^CONFIG_(NET_WIREGUARD|SYSTEM_WG|CRYPTO_CURVE25519|IOB_NCHAINS)=" \
  "${BUILD}/.config"

echo "=== building with CMake/Ninja ==="
cmake --build "${BUILD}" >/tmp/cmake-build.log 2>&1 && rc=0 || rc=$?
# "error:" with the colon, so the progress lines for lib_perror.c and friends
# do not look like failures.
grep -E "error:|undefined reference|No rule to make|ninja: error" \
  /tmp/cmake-build.log | head -30 || true
echo "CMAKE_BUILD_EXIT=$rc"
[ "$rc" -eq 0 ] || exit "$rc"

# The point of the exercise: the driver's own objects must be in the build, and
# the command must be in the binary. A configure-time glob that silently missed
# the directory would still produce a clean build and a device that never
# registers.
echo "=== driver objects produced by the CMake build ==="
found=0
for obj in wireguard wg_crypto wg_noise wg_tai64n; do
  if find "${BUILD}" -name "${obj}.c.o" | grep -q .; then
    echo "  ok   ${obj}.c.o"
    found=$((found + 1))
  else
    echo "  FAIL ${obj}.c.o was not compiled"
  fi
done
[ "${found}" -eq 4 ] ||
  { echo "FAIL: the CMake build did not compile the driver"; exit 1; }

if find "${BUILD}" -name "wg_main.c.o" | grep -q .; then
  echo "  ok   wg_main.c.o (apps/system/wg)"
else
  echo "FAIL: the CMake build did not compile apps/system/wg"
  exit 1
fi

echo "=== image ==="
ls -l "${BUILD}/nuttx" | awk '{print $NF, $5}'
echo "PASS: the CMake path builds the driver and the command (T8)"
