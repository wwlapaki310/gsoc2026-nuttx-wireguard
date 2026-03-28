# GSoC 2026 — WireGuard Port to Apache NuttX

> **Discussion:** [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548)

| | |
|---|---|
| **Organization** | [Apache Software Foundation](https://summerofcode.withgoogle.com/programs/2026/organizations/apache-software-foundation) |
| **Difficulty** | Major |
| **Size** | ~175 hours (Medium) |
| **Mentors** | Alan Carvalho de Assis (acassis@apache.org), dev@nuttx.apache.org |
| **Reference** | [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip), [ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) |

---

## Project Overview

WireGuard is a modern, lightweight VPN protocol originally developed for Linux, and increasingly adopted in embedded and IoT systems. It establishes encrypted tunnels over UDP using state-of-the-art cryptography (Curve25519, ChaCha20-Poly1305, BLAKE2s), while keeping the implementation small enough to run on microcontrollers.

Apache NuttX is a POSIX-compliant RTOS with its own TCP/IP stack and BSD socket interface. However, NuttX currently has no VPN capability. This project implements WireGuard as a NuttX network device, enabling secure remote access to NuttX-based devices.

### Why This Matters

Remote and secure access to NuttX devices is a real, unsolved problem across many domains:

- **Edge AI and industrial IoT** — devices deployed in the field need firmware updates and remote diagnostics without physical access
- **Satellite and space hardware** — NuttX is used in small satellite projects; once launched, the only maintenance path is through the network
- **Unmanned infrastructure** — sensors in remote locations (ocean buoys, mountain stations, pipelines) require secure bidirectional communication
- **Secure device mesh** — NuttX devices can communicate directly with each other through an encrypted tunnel without relying on cloud relay

WireGuard's small footprint and simple key model make it particularly well-suited for these constrained environments.

### Architecture

WireGuard is implemented as a NuttX network device (`wg0`). The porting work is split into three layers:

```
+-----------------------------------------------+
|                  NuttX RTOS                   |
|                                               |
|  Application / NSH                            |
|           |                                   |
|    NuttX Network Stack (BSD socket API)       |
|       |              |                        |
|  eth0 / wlan0       wg0  ← this project       |
|  (physical NIC)  (WireGuard netdev)           |
|                      |                        |
|         +------------+------------+           |
|         |            |            |           |
|   wireguard.c   wireguardif.c  nuttx-         |
|   + crypto/     (logic reused) platform.c     |
|   unchanged     lwIP API calls  (clock,       |
|                 → NuttX API     urandom)      |
|                      |                        |
|              UDP socket (port 51820)          |
+-----------------------------------------------+
              |
    Internet / LTE / satellite link
              |
    WireGuard peer (Linux server)
```

---

## Development Environment

This repository includes a Docker-based development environment with two build targets.

```bash
# SIM — primary development environment (fast iteration)
docker build --target sim -t nuttx-wireguard:sim .
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wireguard:sim

# QEMU — RTOS verification
docker build --target qemu -t nuttx-wireguard:qemu .
docker run --rm -it nuttx-wireguard:qemu
```

See [docs/dev-environment.md](docs/dev-environment.md) for details on each environment.

---

## Reference Implementations

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip)**

The primary source for this port. The codebase has three layers with different porting costs:

| File | Role | Porting cost |
|------|------|--------------|
| `wireguard.c` + `crypto/` | WireGuard protocol and crypto | None — portable C, used as-is |
| `wireguard-platform.h` | OS-specific functions (clock, random, timer) | Low — replace with NuttX POSIX API |
| `wireguardif.c` | Network integration layer (lwIP API calls) | Medium — reuse logic, replace lwIP API with NuttX netdev + BSD socket |

NuttX has its own TCP/IP stack and does not use lwIP, so `wireguardif.c` cannot be compiled as-is. However, the protocol logic (packet encryption/decryption, handshake flow) is fully reusable. The lwIP API calls (`netif_add`, `udp_new`, `pbuf_alloc`, etc.) are replaced with their NuttX equivalents in `nuttx-wireguardif.c`.

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino)**

A prior port of wireguard-lwip to ESP32 (FreeRTOS + ESP-IDF). Studied as a reference for OS-specific adaptations — timer API, random number generation, and task management patterns in an embedded RTOS environment.

---

## Project Timeline

> **Note:** This timeline is tentative and subject to change based on discussion with mentors.

The applicant is based in Japan (JST, UTC+9). Available approximately **12 hours per week** (weekday evenings + weekends). Unavailable August 8–15.

| Period | Dates | Phase |
|--------|-------|-------|
| Community Bonding | May 8 – Jun 1 | Phase 0 |
| Weeks 1–2 | Jun 2 – Jun 13 | Phase 1 ✅ |
| Weeks 3–6 | Jun 16 – Jul 11 | Phase 2 |
| Weeks 7–9 | Jul 14 – Aug 1 | Phase 3 ★ Midterm (Jul 14–18) |
| *(unavailable)* | Aug 8 – Aug 15 | — |
| Weeks 10–12 | Aug 4–8, Aug 18–25 | Phase 4 |
| Post-GSoC | Sep 1 – Sep 27 | Phase 5 |
| Presentation | Oct 11–14 | Community Over Code Glasgow |

---

### Phase 0 — Preparation (Community Bonding: May 8 – Jun 1)

**Goal:** Understand the codebase and establish the development environment before coding begins.

**Already completed (pre-application):**
- Docker images for SIM and QEMU build targets are working (see `Dockerfile`)
- SIM boots to `nsh>` and `ifconfig` shows `eth0 (10.0.0.2)`
- `qemu-armv7a` boots to `nsh>`
- `wireguard-lwip` source integrated into `apps/netutils/wireguard/`; `CONFIG_NET_WIREGUARD=y` builds successfully
- Identified that `wireguardif.c` cannot be compiled as-is (NuttX does not have lwIP headers); created minimal shim headers under `lwip/` and a stub `nuttx-wireguardif.c` to unblock the build (see [docs/phase1-log.md](docs/phase1-log.md))

**To complete during community bonding:**
- Map all lwIP API calls in `wireguardif.c` to their NuttX equivalents
- Design the full `nuttx-wireguardif.c` implementation

**Deliverable:** API mapping documented; `nuttx-wireguardif.c` design complete.

---

### Phase 1 — Build System Integration (Weeks 1–2: Jun 2 – Jun 13)

**Environment:** SIM (`sim:nsh`)

**Status: ✅ Completed (pre-GSoC)**

**Goal:** `CONFIG_NET_WIREGUARD=y` builds successfully and NuttX boots to `nsh>` without errors.

wireguard-lwip provides only `.c`/`.h` source files with no standalone build system. NuttX uses CMake + Kconfig + make; a directory under `apps/netutils/wireguard/` requires `CMakeLists.txt`, `Make.defs`, `Makefile`, and `Kconfig` to be recognized by the build system.

The porting challenge is OS API differences — not the protocol logic itself. `wireguard.c` and `crypto/` are portable C and require no changes.

- `apps/netutils/wireguard/` directory with build files in place
- `wireguard.c`, `crypto/`, `nuttx-platform.c` (stub), `nuttx-wireguardif.c` (stub) compile cleanly
- `CONFIG_NET_WIREGUARD=y` build succeeds; SIM boots to `nsh>`

**Deliverable:** `CONFIG_NET_WIREGUARD=y` build succeeds and NuttX reaches `nsh>`.

---

### Phase 2 — NuttX Integration Layer on SIM (Weeks 3–6: Jun 16 – Jul 11)

**Environment:** SIM (`sim:nsh` with `CONFIG_NET=y`, `CONFIG_SIM_NETDEV=y`)

**Goal:** `nsh> ifconfig` shows `wg0` alongside `eth0`.

This phase has two parts.

**Part A — Platform layer (`nuttx-platform.c`):**

| Function | Purpose | NuttX implementation |
|----------|---------|---------------------|
| `wireguard_sys_now()` | Monotonic ms counter for timers | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | Cryptographic random for key generation | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | TAI64N timestamp for replay prevention | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | Cookie reply decision | `return false` |

**Part B — Network integration layer (`nuttx-wireguardif.c`):**

The protocol logic from `wireguardif.c` is reused. Only the lwIP API calls are replaced with their NuttX equivalents:

| wireguardif.c (lwIP) | nuttx-wireguardif.c (NuttX) |
|----------------------|-----------------------------|
| `struct netif` | `struct net_driver_s` |
| `netif->output = fn` | `dev->d_ifup = fn` equiv. |
| `ip_input(pbuf, netif)` | `devif_input(dev)` |
| `netif_set_link_up()` | `netdev_carrier_on()` |
| `udp_new()` / `udp_bind()` / `udp_recv()` | BSD `socket()` / `bind()` / `recvfrom()` |
| `pbuf_alloc()` / `pbuf_free()` | `iob_alloc()` / `iob_free()` |
| `sys_timeout()` | `wd_start()` |

**Deliverable:** `nsh> ifconfig` shows `wg0` on SIM.

---

### Phase 3 — Handshake and Tunnel on QEMU (Weeks 7–9: Jul 14 – Aug 1) ★ Midterm (Jul 14–18)

**Environment:** `qemu-armv7a`

**Goal:** Complete a WireGuard handshake with a Linux peer and pass traffic through the encrypted tunnel.

SIM shares the host Linux scheduler. QEMU introduces an emulated ARM CPU with NuttX's own scheduler, necessary to verify timer behavior and task scheduling with WireGuard's periodic operations (keepalive, handshake expiry).

- Port the working SIM configuration to `qemu-armv7a`
- Generate key pairs on both NuttX (QEMU) and Linux sides
- Verify Noise protocol handshake over UDP port 51820
- Test end-to-end: `nsh> ping 10.0.0.1`

**Deliverable:**
```
# Linux:
$ sudo wg show
peer: <NuttX public key>
  latest handshake: 3 seconds ago

# NuttX (QEMU):
nsh> ping 10.0.0.1
64 bytes from 10.0.0.1: icmp_seq=0 time=4 ms
```

---

### Phase 4 — NSH Command and Real Hardware (Weeks 10–12: Aug 4–8, Aug 18–25)

**Environment:** ESP32-S3

**Goal:** Add a `wg` shell command to NSH and validate on real hardware.

**NSH command:**
- Implement `wg show` and `wg setconf` as NSH built-in commands
- Finalize Kconfig dependency chain: `NET_WIREGUARD` depends on `NET`, `NET_UDP`, `MBEDTLS`

```
nsh> wg show
interface: wg0
  public key: <base64>
  listening port: 51820
peer: <base64>
  endpoint: 192.168.x.x:51820
  latest handshake: 5 seconds ago
  transfer: 1.23 KiB received, 0.45 KiB sent
```

**Real hardware test (ESP32-S3):**

QEMU uses virtio-net; ESP32-S3 uses a Wi-Fi driver. Real hardware testing validates the netdev integration with a physical network interface.

- Flash NuttX + WireGuard image to ESP32-S3
- Connect to Wi-Fi and verify `wg0` comes up alongside `wlan0`
- Establish a WireGuard tunnel to a Linux peer over Wi-Fi
- Verify `nsh> ping` through the tunnel
- Measure Flash and RAM usage

**Deliverable:** WireGuard tunnel working on ESP32-S3 over Wi-Fi.

---

### Phase 5 — Upstream PR (Post-GSoC: Sep 1 – Sep 27)

**Goal:** Submit a pull request to `apache/nuttx-apps`.

- Sign Apache CLA
- Submit PR to `apps/netutils/wireguard/` conforming to NuttX coding style
- Address review feedback

**Deliverable:** PR open on `apache/nuttx-apps`.

---

### Community Over Code Glasgow (Oct 11–14)

Present the project at the [NuttX International Workshop](https://communityovercode.org/) co-located with Community Over Code Glasgow 2026.

---

## About Me

I work as an EdgeAI engineer at Sony Semiconductor Solutions, where I use NuttX from the application side — primarily with SPRESENSE and ESP32-based edge AI camera systems. Being able to securely access NuttX boards remotely for debugging and maintenance is something I have wanted myself, so this project aligns naturally with my daily work.

**Relevant experience:**

- NuttX: daily use with SPRESENSE and ESP32
- Embedded C: cross-compilation with `arm-none-eabi-gcc`, POSIX API on RTOS
- Docker + QEMU: working `sim` and `qemu-armv7a` development environments set up for this project (see repository)
- Certifications: GCP, AWS, TensorFlow Developer, Information Security Specialist (Japan)

GitHub: [https://github.com/wwlapaki310](https://github.com/wwlapaki310)
