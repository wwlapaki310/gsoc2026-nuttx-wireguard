#!/usr/bin/env bash
# Host C test against the real allocator in the sibling NuttX source tree.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
nuttx="${1:-${here}/../../../nuttx}"
if [ ! -f "${nuttx}/drivers/net/wireguard/wg_tai64n.c" ]; then
  echo "SKIP: requires the NuttX realtime allocator correction (see tai64n-design.md)"
  exit 77
fi
binary="$(mktemp /tmp/wg-tai64n-unit.XXXXXX)"
trap 'rm -f "${binary}"' EXIT
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -pthread -fsanitize=undefined \
  -I"${here}/tai64n-test/include" -I"${nuttx}/drivers/net/wireguard" \
  "${here}/tai64n-test/test.c" "${nuttx}/drivers/net/wireguard/wg_tai64n.c" \
  -o "${binary}"
"${binary}"
