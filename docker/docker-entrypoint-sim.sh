#!/bin/bash
set -e

NUTTX=/opt/nuttx/nuttx

if [ ! -f "$NUTTX" ]; then
  echo "ERROR: $NUTTX not found."
  exit 1
fi

echo "NuttX sim (Linux process simulator)"
echo "  binary : $NUTTX"
echo "  network: TUN/TAP (requires --cap-add=NET_ADMIN --device=/dev/net/tun)"
echo "  exit   : Ctrl-C or 'poweroff' in nsh"

exec "$NUTTX"
