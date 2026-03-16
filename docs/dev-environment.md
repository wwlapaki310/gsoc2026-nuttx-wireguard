# Development Environment

This project uses three environments in order of iteration speed.

```
SIM  (sim:nsh / sim:net)   fastest   NuttX runs as a Linux process on the host
QEMU (qemu-armv7a)         medium    emulated ARM CPU
HW   (ESP32-S3)            slowest   real hardware, final validation
```

Feature development is done on SIM first, then verified on QEMU, then validated on real hardware.

---

## SIM: sim:nsh and sim:net

NuttX has a simulator target (`sim`) that compiles NuttX into a normal Linux process. There is no emulated CPU — it runs directly on the host machine using the host's x86 processor and Linux kernel. This makes the build-run-test cycle very fast.

`sim:nsh` and `sim:net` are two configuration variants of the simulator:

**`sim:nsh`**

Basic simulator with NSH (NuttX Shell) only. Networking is disabled. Used to verify that the build system integration and basic initialization work correctly.

```
$ ./tools/configure.sh sim:nsh
$ make -j$(nproc)
$ ./nuttx
NuttShell (NSH) NuttX-12.7.0
nsh>
```

**`sim:net`**

Simulator with networking enabled. NuttX connects to the host network via a TUN/TAP virtual network interface provided by the Linux kernel. Used to verify lwIP-level networking, UDP sockets, and WireGuard netif registration without needing QEMU or real hardware.

```
$ ./tools/configure.sh sim:net
$ make -j$(nproc)
$ sudo ./nuttx          # root required to create TUN/TAP device
NuttShell (NSH) NuttX-12.7.0
nsh> ifconfig
eth0    HWaddr 00:e0:de:ad:be:ef at UP
        IPaddr:10.0.0.2 DRaddr:10.0.0.1 Mask:255.255.255.0
```

### What SIM does not cover

Because SIM runs on the host Linux scheduler, it does not replicate NuttX's own task scheduling, interrupt timing, or priority preemption. Issues caused by RTOS-specific behavior (timer granularity, task priority interactions, interrupt latency) will not appear on SIM. These are caught in the QEMU stage.

Also, `/dev/urandom` on SIM is the host Linux device, which always works. On QEMU or real hardware, `CONFIG_DEV_RANDOM=y` must be correctly enabled or `wireguard_random_bytes()` will fail.

---

## QEMU: qemu-armv7a

QEMU emulates an ARM Cortex-A7 CPU running NuttX. Unlike SIM, NuttX's own scheduler and interrupt handling are active. Network connectivity is provided by a virtio-net virtual NIC, which is different from the TUN/TAP used by SIM and from the Wi-Fi driver on real ESP32-S3 hardware.

QEMU is used to verify that WireGuard's time-sensitive operations (handshake timeouts, keepalive timers) behave correctly under NuttX's scheduler.

```
$ ./tools/configure.sh qemu-armv7a:nsh
$ make -j$(nproc)
$ qemu-system-arm -M virt -cpu cortex-a7 -nographic -bios none \
    -kernel nuttx \
    -net nic,model=virtio \
    -net user,hostfwd=udp::51820-:51820
```

A Docker-based environment with this setup is available in the `Dockerfile` in the repository root.

---

## Real Hardware: ESP32-S3

ESP32-S3 uses a Wi-Fi driver to connect to lwIP, which is a different path from both virtio-net (QEMU) and TUN/TAP (SIM). Real hardware testing validates the netif integration with a physical interface and provides actual Flash and RAM measurements.

Real hardware testing is deferred to Phase 4 of the project timeline.

---

## Summary: what each environment catches

| Issue | SIM | QEMU | HW |
|-------|-----|------|----|
| Build system errors | ✅ | ✅ | ✅ |
| OS API errors (pthread, clock, urandom) | ✅ | ✅ | ✅ |
| lwIP netif registration | ✅ (sim:net) | ✅ | ✅ |
| WireGuard handshake (UDP) | ✅ (sim:net) | ✅ | ✅ |
| RTOS scheduler / timer behavior | ❌ | ✅ | ✅ |
| /dev/urandom availability | ❌ (host always works) | ✅ | ✅ |
| Wi-Fi driver + netif coexistence | ❌ | ❌ | ✅ |
| Flash / RAM usage | ❌ | ❌ | ✅ |
