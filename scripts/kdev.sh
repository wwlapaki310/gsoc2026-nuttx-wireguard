#!/usr/bin/env bash
# Kernel-side development loop against the persistent "wgdev" container.
#
#   scripts/kdev.sh sync            copy the fork's working-tree diff into the container
#   scripts/kdev.sh configure       sim:nsh + WireGuard options (once, or after Kconfig edits)
#   scripts/kdev.sh build           make (incremental)
#   scripts/kdev.sh style           checkpatch on the WireGuard files
#   scripts/kdev.sh nsh 'cmd1;cmd2' run nuttx sim, feed NSH commands, print output
#   scripts/kdev.sh test <script>   run a scripts/verify-*.sh inside the container
#
# The container was created from nuttx-wireguard:sim-master with /opt/nuttx
# and /opt/apps checked out at the same upstream commits the forks are based
# on; "sync" applies each fork's diff against its merge-base with
# upstream/master on top (so upstream moving on does not matter).
set -euo pipefail

NUTTX_FORK="${NUTTX_FORK:-/c/Users/wwlap/workspace/nuttx}"
APPS_FORK="${APPS_FORK:-/c/Users/wwlap/workspace/nuttx-apps}"
C=wgdev
dx() { MSYS_NO_PATHCONV=1 docker exec "$C" bash -c "$*"; }

case "${1:-}" in
  sync)
    (cd "$NUTTX_FORK" && git diff --binary "$(git merge-base HEAD upstream/master)") > /tmp/nuttx.patch
    (cd "$APPS_FORK"  && git diff --binary "$(git merge-base HEAD upstream/master)") > /tmp/apps.patch
    docker cp /tmp/nuttx.patch "$C":/tmp/nuttx.patch
    docker cp /tmp/apps.patch  "$C":/tmp/apps.patch
    dx 'cd /opt/nuttx && git checkout -q -- . && rm -rf drivers/net/wireguard include/nuttx/net/wireguard.h boards/sim/sim/sim/configs/wireguard && if [ -s /tmp/nuttx.patch ]; then git apply /tmp/nuttx.patch; fi && git status --short | head -20'
    dx 'cd /opt/apps && git checkout -q -- . && rm -rf system/wg && if [ -s /tmp/apps.patch ]; then git apply /tmp/apps.patch; fi && rm -rf netutils/wireguard && (cd netutils && bash ../tools/mkkconfig.sh -m "Network Utilities" -o Kconfig >/dev/null) && (cd system && bash ../tools/mkkconfig.sh -m "System Libraries and NSH Add-Ons" -o Kconfig >/dev/null) && git status --short | head'
    ;;
  configure)
    dx 'cd /opt/nuttx && make distclean >/dev/null 2>&1; ./tools/configure.sh sim:nsh >/dev/null && \
      for o in NET NET_IPv4 NET_UDP NET_TCP NET_ICMP NET_ICMP_SOCKET SIM_NETDEV NETUTILS_IFCONFIG NETUTILS_PING ALLOW_BSD_COMPONENTS NET_SOCKOPTS DEV_URANDOM CRYPTO CRYPTO_RANDOM_POOL DEV_URANDOM_RANDOM_POOL NET_WIREGUARD SYSTEM_WG; do kconfig-tweak --enable CONFIG_$o >/dev/null; done; \
      kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128; kconfig-tweak --set-val CONFIG_NSH_LINELEN 160; kconfig-tweak --set-val CONFIG_LINE_MAX 160; kconfig-tweak --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4; kconfig-tweak --set-str CONFIG_SYSTEM_WG_CONFIG_PATH /tmp/wg0.conf; \
      make olddefconfig >/dev/null 2>&1; grep -E "^CONFIG_(NET_WIREGUARD|SYSTEM_WG|CRYPTO_CURVE25519|NETDEV_IOCTL|DEV_URANDOM)" .config'
    ;;
  build)
    dx 'cd /opt/nuttx && make -j$(nproc) >/tmp/build.log 2>&1; rc=$?; grep -E "error|warning: .*(wireguard|wg_)" /tmp/build.log | grep -v "^ *$" | head -40; echo "BUILD_EXIT=$rc"; ls -la nuttx 2>/dev/null | cut -c1-60; exit $rc'
    ;;
  style)
    dx 'cd /opt/nuttx && ./tools/checkpatch.sh -f drivers/net/wireguard/*.c drivers/net/wireguard/*.h include/nuttx/net/wireguard.h 2>&1 | tail -40; cd /opt/apps && ../nuttx/tools/checkpatch.sh -f system/wg/*.c 2>&1 | tail -20'
    ;;
  nsh)
    cmds="${2:-help}"
    dx "cd /opt/nuttx && rm -f /tmp/n.in && mkfifo /tmp/n.in && (./nuttx </tmp/n.in >/tmp/n.out 2>&1 &) && exec 3>/tmp/n.in && sleep 2 && printf '%s\n' \"\$(echo '$cmds' | tr ';' '\n')\" >&3 && sleep 3 && printf 'poweroff\n' >&3; sleep 1; sed 's/\x1b\[K//g' /tmp/n.out | tail -40"
    ;;
  test)
    docker cp "$(dirname "$0")/$2" "$C":/tmp/t.sh
    dx "bash /tmp/t.sh >/tmp/t.out 2>&1; rc=\$?; sed 's/\x1b\[K//g' /tmp/t.out | grep -vE '^\s*\$' | tail -${3:-30}; exit \$rc"
    ;;
  *)
    sed -n 2,12p "$0"; exit 1;;
esac
