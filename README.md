# GSoC 2026 — WireGuard Port to Apache NuttX

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

Apache NuttX is a POSIX-compliant RTOS widely used in embedded systems, supporting networking via the lwIP TCP/IP stack. However, NuttX currently has no VPN capability.

This project ports WireGuard to NuttX by implementing it as a **lwIP virtual network interface (netif)**. The primary reference is [wireguard-lwip](https://github.com/smartalock/wireguard-lwip), which already provides a lwIP-based WireGuard implementation, making it a natural starting point for NuttX porting.

### Why This Matters

Remote and secure access to NuttX devices is a real, unsolved problem across many domains:

- **Edge AI and industrial IoT** — devices deployed in the field need firmware updates and remote diagnostics without physical access
- **Satellite and space hardware** — NuttX is used in small satellite projects; once launched, the only maintenance path is through the network
- **Unmanned infrastructure** — sensors in remote locations (ocean buoys, mountain stations, pipelines) require secure bidirectional communication
- **Secure device mesh** — NuttX devices can communicate directly with each other through an encrypted tunnel without relying on cloud relay

WireGuard's small footprint and simple key model make it particularly well-suited for these constrained environments.

### Architecture

```
+----------------------------------+
|          NuttX RTOS              |
|                                  |
|  Application / NSH               |
|           |                      |
|      lwIP TCP/IP Stack           |
|       |            |             |
|   eth0 / wlan0    wg0            |  ← WireGuard netif (this project)
|   (physical NIC) (VPN tunnel)    |
|                   |              |
|        UDP socket (port 51820)   |
+----------------------------------+
            |
     Internet / LTE / satellite link
            |
     WireGuard peer (Linux server)
```

---

## Development Environment

This repository includes a Docker-based development environment with two build targets.

```bash
# sim:nsh + NET — primary development environment (fast iteration)
docker build --target sim -t nuttx-wireguard:sim .
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wireguard:sim

# qemu-armv7a — RTOS verification
docker build --target qemu -t nuttx-wireguard:qemu .
docker run --rm -it nuttx-wireguard:qemu
```

See [docs/dev-environment.md](docs/dev-environment.md) for details.

---

## Reference Implementations

Two existing projects are used as references, each serving a different role.

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) — the code base to port**

This is the actual code being brought into NuttX. It implements WireGuard as a lwIP netif and isolates all OS-specific behavior behind a four-function platform abstraction (`wireguard-platform.h`). The WireGuard protocol logic (`wireguard.c`) and cryptographic primitives (`crypto/`) require no OS dependencies and can be used as-is. The porting work is focused on implementing `wireguard-platform.h` for NuttX and removing platform-specific code from `wireguardif.c`.

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) — a prior port to study**

This project ports wireguard-lwip to ESP32 (FreeRTOS + ESP-IDF). Since NuttX is also an embedded RTOS with lwIP, the changes made in this ESP32 port — replacing FreeRTOS primitives, adapting logging, removing platform-specific headers — serve as a concrete reference for what needs to change when porting wireguard-lwip to a new target.

---

## Project Timeline

> **Note:** This timeline is tentative and subject to change based on discussion with mentors.

The applicant is based in Japan (JST, UTC+9). The applicant will be unavailable from August 8 to August 15.

### Phase 0 — Preparation (Pre-GSoC / Community Bonding)

**Goal:** Build the understanding and environment needed before writing any NuttX-specific code.

**Already completed (pre-application):**
- Set up a Docker + QEMU development environment running NuttX `qemu-armv7a:nsh` with networking enabled (see `Dockerfile` and [docs/dev-environment.md](docs/dev-environment.md))
- Read through wireguard-lwip and WireGuard-ESP32-Arduino source code to understand the porting scope

**To complete during community bonding:**
- Identify all OS-specific API calls in wireguard-lwip that need to be replaced for NuttX (threads, mutexes, time, random)
- Study the ESP32 port as a diff: understand exactly what changed from wireguard-lwip to make it run on FreeRTOS + ESP-IDF, then map each change to its NuttX equivalent
- Set up `sim:net` as the primary development environment for feature work

**Deliverable:** A documented list of APIs to replace and a working build loop on SIM.

---

### Phase 1 — Build System Integration on SIM (Weeks 1–2)

**Environment:** `sim:nsh` (see [docs/dev-environment.md](docs/dev-environment.md))

**Goal:** Add wireguard-lwip to NuttX's build system and confirm NuttX still boots without errors.

wireguard-lwip provides only `.c`/`.h` source files and has no standalone build system — it is designed to be incorporated into the target platform's existing build system. NuttX uses CMake + Kconfig + make; a source directory under `apps/netutils/wireguard/` is only recognized by the build system when accompanied by `CMakeLists.txt` (or `Make.defs`) and a `Kconfig` file.

The porting challenge in this project is fundamentally about OS API differences. wireguard-lwip's core logic (`wireguard.c`, `crypto/`) is written in portable C with no OS or ISA dependencies and requires no changes. OS-specific adaptation is handled in Phase 2 via `wireguard-platform.h`.

The goal of this phase is simply:

```
CONFIG_WIREGUARD=y
  make
    -> build success
    -> boot success (nsh> reached, no crash)
```

- Place wireguard-lwip sources under `apps/netutils/wireguard/`
- Write `CMakeLists.txt` and `Make.defs` following NuttX conventions
- Add `Kconfig` entry: `CONFIG_NET_WIREGUARD`
- Confirm NuttX boots to `nsh>` on SIM without crashing

**Deliverable:** `CONFIG_WIREGUARD=y` build succeeds and NuttX reaches `nsh>` without errors.

---

### Phase 2 — NuttX Platform Layer and lwIP Integration on SIM (Weeks 3–6)

**Environment:** `sim:net`

**Goal:** Implement the platform abstraction layer and register WireGuard as a lwIP netif, verified on `sim:net`.

**Platform layer (`wireguard-platform.h`):**

| Function | Purpose | NuttX implementation |
|----------|---------|---------------------|
| `wireguard_sys_now()` | Monotonic ms counter for timers | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | Cryptographic random for key generation | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | TAI64N timestamp for replay prevention | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | Cookie reply decision | `return false` (sufficient for embedded) |

- Create `nuttx-platform.c` with the four platform functions
- Replace ESP-specific logging (`ESP_LOGI` etc.) with `syslog()`
- Remove ESP-specific headers (`esp_netif.h`, `tcpip_adapter.h`)
- Call `wireguardif_init()` during NuttX startup and register `wg0` via `netif_add()`
- Configure WireGuard parameters (private key, listen port) via Kconfig

**Deliverable:** `nsh> ifconfig` shows `wg0` on `sim:net`.

---

### Phase 3 — Handshake and Tunnel on QEMU (Weeks 7–9) ★ Midterm

**Environment:** `qemu-armv7a`

**Goal:** Verify RTOS behavior under real scheduling conditions and complete a WireGuard handshake with a Linux peer.

SIM shares the host Linux scheduler. QEMU introduces an emulated ARM CPU with NuttX's own scheduler, which is necessary to confirm that timer behavior, task priorities, and interrupt handling work correctly with WireGuard's periodic tasks (keepalive, handshake expiry).

- Port the working `sim:net` configuration to `qemu-armv7a`
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

### Phase 4 — NSH Command, Kconfig, and Real Hardware (Weeks 10–11)

**Environment:** ESP32-S3

**Goal:** Add a `wg` shell command to NSH and validate the implementation on real hardware.

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

QEMU uses virtio-net to connect to lwIP. ESP32-S3 uses a Wi-Fi driver instead. Testing on real hardware validates the netif integration with an actual physical network interface.

- Flash NuttX + WireGuard image to ESP32-S3
- Connect to Wi-Fi and verify `wg0` comes up alongside `wlan0`
- Establish a WireGuard tunnel to a Linux peer over Wi-Fi
- Verify `nsh> ping` through the tunnel
- Measure Flash and RAM usage on actual hardware

**Deliverable:** WireGuard tunnel working on ESP32-S3 over Wi-Fi.

---

### Phase 5 — Upstream PR (Week 12)

**Goal:** Submit a pull request to `apache/nuttx-apps`.

- Sign Apache CLA
- Submit PR to `apps/netutils/wireguard/` conforming to NuttX coding style

**Deliverable:** PR open on `apache/nuttx-apps`.

---

## About Me

I work as an EdgeAI engineer at Sony Semiconductor Solutions, where I use NuttX from the application side — primarily with SPRESENSE and ESP32-based edge AI camera systems. Being able to securely access NuttX boards remotely for debugging and maintenance is something I have wanted myself, so this project aligns naturally with my daily work.

**Relevant experience:**

- NuttX: daily use with SPRESENSE and ESP32
- Embedded C: cross-compilation with `arm-none-eabi-gcc`, POSIX API on RTOS
- Docker + QEMU: set up a working `qemu-armv7a:nsh` development environment for this project (see repository)
- Certifications: GCP, AWS, TensorFlow Developer, Information Security Specialist (Japan)

GitHub: [https://github.com/wwlapaki310](https://github.com/wwlapaki310)
