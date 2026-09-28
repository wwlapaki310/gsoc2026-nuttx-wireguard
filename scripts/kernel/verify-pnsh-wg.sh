#!/usr/bin/env bash
# PROTECTED *runtime*: rv-virt:pnsh64 booted in QEMU with the MPU split, the wg
# command living in nuttx_user behind that split, tunnelling to a real Linux
# kernel WireGuard peer over virtio-net.
#
# Until now PROTECTED was only ever a build (mr-canhubk3:knsh links, but that
# board has no emulator). The thing a build cannot show is whether the ioctl
# ABI actually survives the boundary: every request is a fixed struct with no
# embedded pointers precisely so the kernel never dereferences a user address,
# and the way that assumption fails is an MPU exception at the first ioctl, not
# a link error. So this run also watches the console for one.
#
#   Host  tapwg  10.0.0.1/24  + Linux wgtest0 (listen 51821), tunnel 10.10.0.1
#   Guest eth0   10.0.0.2/24  + NuttX wg0     (listen 51820), tunnel 10.10.0.2
#
# pnsh64 is an M-mode build, so QEMU runs it with -bios none, and the user-space
# blob has to be loaded separately with -device loader.
#
# Run inside the wgdev container after scripts/kernel/build-pnsh-wg.sh.
set -euo pipefail

cd /opt/nuttx

[ -f nuttx ] && [ -f nuttx_user ] ||
  { echo "FAIL: build-pnsh-wg.sh has not produced nuttx and nuttx_user"; exit 1; }
grep -q "^CONFIG_BUILD_PROTECTED=y" .config ||
  { echo "FAIL: this is not a BUILD_PROTECTED configuration"; exit 1; }

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

ip link del wgtest0 2>/dev/null || true
ip tuntap add dev tapwg mode tap 2>/dev/null || true
ip addr flush dev tapwg 2>/dev/null || true
ip addr add 10.0.0.1/24 dev tapwg
ip link set tapwg up

linux_priv="$(wg genkey)"
linux_pub="$(printf "%s" "${linux_priv}" | wg pubkey)"
echo "Linux public key: ${linux_pub}"

rm -f /tmp/p.in /tmp/p.out
mkfifo /tmp/p.in

qemu-system-riscv64 -semihosting -M virt,aclint=on -cpu rv64 -smp 1 \
  -bios none -kernel nuttx -device loader,file=./nuttx_user \
  -serial mon:stdio -display none \
  -global virtio-mmio.force-legacy=false \
  -netdev tap,id=u1,ifname=tapwg,script=no,downscript=no \
  -device virtio-net-device,netdev=u1,bus=virtio-mmio-bus.0 \
  </tmp/p.in >/tmp/p.out 2>&1 &
qemu_pid=$!

cleanup() {
  set +e
  printf "poweroff\n" >&3 2>/dev/null
  sleep 1
  kill "${qemu_pid}" 2>/dev/null
  wait "${qemu_pid}" 2>/dev/null
  ip link del wgtest0 2>/dev/null
  rm -f /tmp/p.in
}
trap cleanup EXIT

exec 3>/tmp/p.in

out() { sed 's/\x1b\[[0-9;]*[A-Za-z]//g' /tmp/p.out; }
# NSH drops the first byte after printing a prompt, so lead with a newline.
send() { printf "\n%s\n" "$1" >&3; sleep 1; }

# Any of these means the MPU boundary was crossed wrongly, which is the whole
# risk this configuration exists to test.
faulted() {
  out | grep -aqE "EXCEPTION|riscv_exception|Data access|Instruction access|"\
"PANIC|Assertion failed|_assert:"
}
show_fault() {
  out | grep -aE "EXCEPTION|riscv_exception|Data access|Instruction access|"\
"PANIC|Assertion failed|_assert:" | head -10
}

echo "Waiting for NSH..."
for _ in $(seq 1 40); do
  out | grep -aq "NuttShell" && break
  sleep 1
done
out | grep -aq "NuttShell" ||
  { echo "FAIL: NSH did not start under BUILD_PROTECTED"; out | tail -40; exit 1; }
echo "PASS: pnsh64 reached a shell with the MPU split and the user blob loaded"
sleep 2

send "ifconfig eth0 10.0.0.2"
send "wg genkey"
sleep 2
nuttx_priv="$(out | grep -aEo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${nuttx_priv}" ] ||
  { echo "FAIL: no key from wg genkey"; out | tail -40; exit 1; }
nuttx_pub="$(printf "%s" "${nuttx_priv}" | wg pubkey)"
echo "NuttX public key: ${nuttx_pub}"
echo "PASS: offline key generation works in user space behind the MPU split"

# Each of these is an ioctl from nuttx_user into the kernel driver. If the ABI
# were not pointer-free, the first one would fault here.
send "wg set private-key ${nuttx_priv}"
send "wg set listen-port 51820"
send "wg set address 10.10.0.2/24"
send "wg set peer ${linux_pub} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 25"
send "wg up"
sleep 2

if faulted; then
  echo "FAIL: the guest faulted while configuring wg0 across the MPU boundary"
  show_fault
  exit 1
fi
echo "PASS: the configuration ioctls crossed the MPU boundary without a fault"

printf "%s\n" "${linux_priv}" > /tmp/linux_private.key
ip link add wgtest0 type wireguard
wg set wgtest0 \
  private-key /tmp/linux_private.key \
  listen-port 51821 \
  peer "${nuttx_pub}" \
  allowed-ips 10.10.0.2/32 \
  endpoint 10.0.0.2:51820 \
  persistent-keepalive 25
ip addr add 10.10.0.1/24 dev wgtest0
ip link set wgtest0 up

echo "Waiting for handshake..."
tunnelled=false
for _ in $(seq 1 10); do
  sleep 2
  if ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1; then tunnelled=true; break; fi
done
if [ "${tunnelled}" != true ]; then
  echo "FAIL: no tunnelled traffic host -> guest under BUILD_PROTECTED"
  echo "--- wgtest0 ---"; wg show wgtest0
  before=$(wc -l </tmp/p.out); send "wg show"; sleep 2
  tail -n +$((before + 1)) /tmp/p.out | sed 's/\x1b\[[0-9;]*[A-Za-z]//g'
  exit 1
fi
ping -c 3 -W 3 10.10.0.2 >/dev/null
echo "PASS: the host reached 10.10.0.2 through the tunnel (handshake + data)"

wg show wgtest0 | grep -q "latest handshake" &&
  echo "PASS: the Linux peer recorded a handshake with the PROTECTED guest"

# wg show is the read direction of the same ABI: the kernel fills a fixed
# struct the user side supplied, including the statistics.
before=$(wc -l </tmp/p.out)
send "wg show"
sleep 2
guest_show="$(tail -n +$((before + 1)) /tmp/p.out |
              sed 's/\x1b\[[0-9;]*[A-Za-z]//g')"
echo "${guest_show}" | grep -aq "latest handshake" ||
  { echo "FAIL: the guest did not report a handshake"; echo "${guest_show}"; exit 1; }
echo "${guest_show}" | grep -aq "B received" ||
  { echo "FAIL: the guest reported no transfer statistics"; exit 1; }
echo "PASS: the guest reports the handshake and its counters back across the split"

# Down and up again: ifdown stops the kernel RX thread while a user-space task
# waits on the ioctl, which is where a boundary mistake would show up late.
send "wg down"
sleep 2
send "wg up"
sleep 2
if faulted; then
  echo "FAIL: the guest faulted during a down/up cycle"
  show_fault
  exit 1
fi
recovered=false
for _ in $(seq 1 10); do
  sleep 2
  if ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1; then recovered=true; break; fi
done
[ "${recovered}" = true ] ||
  { echo "FAIL: the tunnel did not come back after down/up"; exit 1; }
echo "PASS: down/up works and the tunnel returns"

if faulted; then
  echo "FAIL: a fault was reported at some point during the run"
  show_fault
  exit 1
fi

echo "PASS: BUILD_PROTECTED runtime verified"\
" (MPU split + virtio-net + real Linux peer)"
