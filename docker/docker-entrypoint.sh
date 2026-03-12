#!/bin/bash
set -e

KERNEL=/opt/nuttx/nuttx

if [ ! -f "$KERNEL" ]; then
  echo "ERROR: $KERNEL not found."
  exit 1
fi

echo "NuttX on QEMU (ARM Cortex-A7)"
echo "  kernel : $KERNEL"
echo "  WireGuard UDP 51820 forwarded to host:51820"
echo "  exit   : Ctrl-A -> X"

exec qemu-system-arm \
  -M virt \
  -cpu cortex-a7 \
  -nographic \
  -bios none \
  -kernel "$KERNEL" \
  -net nic,model=virtio \
  -net user,hostfwd=udp::51820-:51820
