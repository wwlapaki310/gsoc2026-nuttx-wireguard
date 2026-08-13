#!/usr/bin/env bash
set -euo pipefail

cd /opt/nuttx

nuttx_priv="$(dd if=/dev/urandom bs=32 count=1 2>/dev/null | base64)"
kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PRIVATE_KEY "${nuttx_priv}"
make olddefconfig >/tmp/qemu-olddefconfig.log
make -j"$(nproc)" >/tmp/qemu-build.log

cd /workspace

rm -f /tmp/qemu.in /tmp/qemu.out
mkfifo /tmp/qemu.in

/usr/local/bin/docker-entrypoint.sh </tmp/qemu.in >/tmp/qemu.out 2>&1 &
qemu_pid=$!

cleanup() {
  set +e
  printf "\001x" >&3 2>/dev/null
  kill "${qemu_pid}" 2>/dev/null
  wait "${qemu_pid}" 2>/dev/null
  rm -f /tmp/qemu.in
}

trap cleanup EXIT

exec 3>/tmp/qemu.in

for _ in $(seq 1 30); do
  if grep -q "nsh>" /tmp/qemu.out; then
    break
  fi
  sleep 1
done

printf "ifconfig\nwg\nifconfig\nwg show\nps\n" >&3
sleep 3

echo "===== QEMU output ====="
sed -n "1,220p" /tmp/qemu.out

if grep -q "eth0" /tmp/qemu.out && grep -q "wg0 is up" /tmp/qemu.out; then
  echo "PASS: QEMU booted and wg0 is visible"
else
  echo "ERROR: QEMU booted, but eth0/wg0 did not come up"
  exit 1
fi
