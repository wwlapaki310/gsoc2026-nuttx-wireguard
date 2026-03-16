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

## Reference Implementations

Two existing projects are used as references, each serving a different role.

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) — the code base to port**

This is the actual code being brought into NuttX. It implements WireGuard as a lwIP netif and isolates all OS-specific behavior behind a four-function platform abstraction (`wireguard-platform.h`). The WireGuard protocol logic (`wireguard.c`) and cryptographic primitives (`crypto/`) require no OS dependencies and can be used as-is. The porting work is focused on implementing `wireguard-platform.h` for NuttX and removing platform-specific code from `wireguardif.c`.

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) — a prior port to study**

This project ports wireguard-lwip to ESP32 (FreeRTOS + ESP-IDF). Since NuttX is also an embedded RTOS with lwIP, the changes made in this ESP32 port — replacing FreeRTOS primitives, adapting logging, removing platform-specific headers — serve as a concrete reference for what needs to change when porting wireguard-lwip to a new target.

---

## Project Timeline

> **Note:** This timeline is tentative and subject to change based on discussion with mentors.

### Phase 0 — Preparation (Pre-GSoC / Community Bonding)

**Goal:** Build the understanding and environment needed before writing any NuttX-specific code.

All development through Phase 4 is done on QEMU rather than real hardware, to keep the iteration cycle fast and hardware-independent. Real hardware testing is deferred to Phase 5.

**Already completed (pre-application):**
- Set up a Docker + QEMU development environment running NuttX `qemu-armv7a:nsh` with networking enabled (see `Dockerfile` in this repository)
- Read through wireguard-lwip and WireGuard-ESP32-Arduino source code to understand the porting scope

**To complete during community bonding:**
- Identify all OS-specific API calls in wireguard-lwip that need to be replaced for NuttX (threads, mutexes, time, random)
- Study the ESP32 port as a diff: understand exactly what changed from wireguard-lwip to make it run on FreeRTOS + ESP-IDF, then map each change to its NuttX equivalent
- Set up a build and test workflow: wireguard source inside `apps/netutils/wireguard/`, compiled and linked into a NuttX image on QEMU, so that changes can be tested iteratively

**Deliverable:** A documented list of APIs to replace and a working build loop on QEMU.

---

### Phase 1 — Build System Integration (Weeks 1–2)

**Goal:** Get wireguard-lwip source files compiling under the NuttX cross-compilation toolchain.

- Place wireguard-lwip sources under `apps/netutils/wireguard/`
- Write `CMakeLists.txt` and `Make.defs` following NuttX conventions
- Resolve `arm-none-eabi-gcc` compiler errors (type definitions, attributes, etc.)
- Add `Kconfig` entry: `CONFIG_NET_WIREGUARD`

**Deliverable:** Zero build errors when wireguard is included in a `sim:nsh` build.

---

### Phase 2 — NuttX Platform Layer (Weeks 3–4)

**Goal:** Implement `wireguard-platform.h` — the platform abstraction that wireguard-lwip requires every port to provide.

| Function | Purpose | NuttX implementation |
|----------|---------|---------------------|
| `wireguard_sys_now()` | Monotonic ms counter for timers | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | Cryptographic random for key generation | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | TAI64N timestamp for replay prevention | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | Cookie reply decision | `return false` (sufficient for embedded) |

- Create `nuttx-platform.c` with the four platform functions
- Replace ESP-specific logging (`ESP_LOGI` etc.) with `syslog()`
- Remove ESP-specific headers (`esp_netif.h`, `tcpip_adapter.h`)

**Deliverable:** `wireguardif_init()` runs on QEMU without crashing.

---

### Phase 3 — lwIP netif Registration (Weeks 5–6)

**Goal:** Register WireGuard as a virtual NIC in NuttX's lwIP stack so that `wg0` appears in `ifconfig`.

- Call `wireguardif_init()` during NuttX startup
- Configure WireGuard parameters (private key, listen port) via Kconfig
- Verify `netif_add()` succeeds

**Deliverable:** `nsh> ifconfig` shows `wg0`.

---

### Phase 4 — Handshake and Tunnel on QEMU (Weeks 7–9) ★ Midterm

**Goal:** Complete a WireGuard handshake with a Linux peer and pass traffic through the encrypted tunnel, on QEMU.

- Generate key pairs on both NuttX (QEMU) and Linux sides
- Configure peer public key and endpoint in NuttX via Kconfig
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

### Phase 5 — NSH Command, Kconfig, and Real Hardware (Weeks 10–11)

**Goal:** Add a `wg` shell command to NSH, and verify the implementation runs on real hardware (ESP32-S3).

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

ESP32-S3 uses a different network stack path than QEMU (Wi-Fi driver → lwIP, rather than virtio-net → lwIP). Testing on real hardware validates that the netif integration works with an actual physical interface.

- Flash NuttX + WireGuard image to ESP32-S3
- Connect ESP32-S3 to Wi-Fi and verify `wg0` comes up alongside `wlan0`
- Establish a WireGuard tunnel to a Linux peer over Wi-Fi
- Verify `nsh> ping` through the tunnel
- Measure Flash and RAM usage on actual hardware

**Deliverable:** WireGuard tunnel working on ESP32-S3 over Wi-Fi.

---

### Phase 6 — Upstream PR (Week 12)

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
