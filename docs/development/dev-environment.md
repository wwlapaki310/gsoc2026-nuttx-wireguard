# Development Environment

## Quick Start (Docker)

All environments run inside a single Docker image. Build once, then choose the target at runtime.

```bash
# sim:nsh + NET — primary development environment
docker build --target sim -t nuttx-wireguard:sim .
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wireguard:sim

# qemu-armv7a — RTOS verification environment
docker build --target qemu -t nuttx-wireguard:qemu .
docker run --rm -it nuttx-wireguard:qemu

# Open a shell for development
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun --entrypoint bash nuttx-wireguard:sim
```

### Rebuilding inside the container

```bash
# sim:net
cd /opt/nuttx
make distclean
./tools/configure.sh sim:net
make -j$(nproc)
./nuttx          # TUN/TAP requires running as root (default in container)

# qemu-armv7a
make distclean
./tools/configure.sh qemu-armv7a:nsh
make -j$(nproc)
```

### Kconfig changes

```bash
cd /opt/nuttx
make menuconfig        # interactive
# or:
kconfig-tweak --enable CONFIG_SOME_OPTION
make olddefconfig
make -j$(nproc)
```

---

This project uses three environments in order of iteration speed.

| Environment | How it runs | Used for |
|-------------|-------------|----------|
| SIM (`sim:nsh`, `sim:net`) | NuttX as a Linux process on the host | Build verification, OS API porting, lwIP integration |
| QEMU (`qemu-armv7a`) | Emulated ARM CPU | RTOS scheduler and timer behavior |
| Real hardware (ESP32-S3) | Physical device | Wi-Fi driver, Flash/RAM validation |

Feature development is done on SIM first, then verified on QEMU, then validated on real hardware.

---

## SIM: sim:nsh and sim:net

NuttX has a simulator target (`sim`) that compiles NuttX into a normal Linux process and runs it directly on the host machine. There is no emulated CPU. This makes the build-run-test cycle very fast.

`nsh` stands for **NuttShell** — the interactive shell of Apache NuttX, equivalent to bash on Linux.

**`sim:nsh`** is the SIM target with NuttShell only. The lwIP network stack is disabled.

```
$ ./tools/configure.sh sim:nsh
$ make -j$(nproc)
$ ./nuttx
NuttShell (NSH) NuttX-12.7.0
nsh>
```

**`sim:net`** is the SIM target with the lwIP network stack enabled. NuttX connects to the host network through the sim network driver, which creates a TUN/TAP virtual interface on the host Linux kernel.

```
$ ./tools/configure.sh sim:net
$ make -j$(nproc)
$ sudo ./nuttx          # root required for TUN/TAP
NuttShell (NSH) NuttX-12.7.0
nsh> ifconfig
eth0    HWaddr 00:e0:de:ad:be:ef at UP
        IPaddr:10.0.0.2 DRaddr:10.0.0.1 Mask:255.255.255.0
```

`sim:net` is the primary environment for WireGuard development in this project, because WireGuard requires UDP and lwIP.

### What SIM does not cover

SIM runs on the host Linux scheduler, not NuttX's own scheduler. RTOS-specific behavior — timer granularity, task priority preemption, interrupt latency — does not appear on SIM and is caught at the QEMU stage.

Also, `/dev/urandom` on SIM is the host Linux device. On QEMU and real hardware, `CONFIG_DEV_RANDOM=y` must be correctly enabled for `wireguard_random_bytes()` to work.

---

## QEMU: qemu-armv7a

QEMU emulates an ARM Cortex-A7 CPU running NuttX. NuttX's own scheduler and interrupt handling are active. Network connectivity uses virtio-net, a virtual NIC that is different from both the TUN/TAP used by SIM and the Wi-Fi driver on real hardware.

QEMU verifies that WireGuard's time-sensitive operations (handshake timeouts, keepalive timers) behave correctly under NuttX's scheduler.

A Docker-based environment for `qemu-armv7a:nsh` is available in the `Dockerfile` in the repository root.

---

## Real Hardware: ESP32-S3

ESP32-S3 connects to lwIP through a Wi-Fi driver — a different path from virtio-net (QEMU) and TUN/TAP (SIM). Real hardware testing validates the netif integration with a physical interface and provides actual Flash and RAM measurements.

---

## What each environment catches

| Issue | SIM | QEMU | HW |
|-------|-----|------|----|
| Build system errors | ✅ | ✅ | ✅ |
| OS API errors (pthread, clock, urandom) | ✅ | ✅ | ✅ |
| lwIP netif registration | ✅ (`sim:net`) | ✅ | ✅ |
| WireGuard handshake (UDP) | ✅ (`sim:net`) | ✅ | ✅ |
| RTOS scheduler / timer behavior | ❌ | ✅ | ✅ |
| `/dev/urandom` availability | ❌ (host always works) | ✅ | ✅ |
| Wi-Fi driver + netif coexistence | ❌ | ❌ | ✅ |
| Flash / RAM usage | ❌ | ❌ | ✅ |
