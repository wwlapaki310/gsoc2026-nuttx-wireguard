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

## Project Timeline

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

### Phase 4 — Handshake and Tunnel (Weeks 7–9) ★ Midterm

**Goal:** Complete a WireGuard handshake with a Linux peer and pass traffic through the encrypted tunnel.

- Generate key pairs on both NuttX and Linux sides
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

### Phase 5 — NSH Command and Kconfig Integration (Weeks 10–11)

**Goal:** Add a `wg` shell command to NSH for runtime status and configuration.

- Implement `wg show` and `wg setconf` as NSH built-in commands
- Finalize Kconfig dependency chain: `NET_WIREGUARD` depends on `NET`, `NET_UDP`, `MBEDTLS`

**Deliverable:**
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

---

### Phase 6 — Hardware Test and Upstream PR (Week 12)

**Goal:** Verify on real hardware and submit a pull request to `apache/nuttx-apps`.

- Test on ESP32-S3 (Wi-Fi netif + WireGuard netif coexistence)
- Measure actual Flash and RAM usage
- Submit PR to `apps/netutils/wireguard/` conforming to NuttX coding style

**Deliverable:** PR open on `apache/nuttx-apps`.

---

## About Me

I work as an EdgeAI engineer at Sony Semiconductor Solutions, developing embedded AI camera systems using NuttX-based boards — primarily SPRESENSE and ESP32. My work spans hardware bring-up, RTOS configuration, lwIP networking, and cloud integration at the edge.

Being able to establish a secure, bidirectional connection to a NuttX device remotely — for firmware updates, diagnostics, or command execution — is something I have needed in my own work. This project directly addresses that gap.

**Relevant experience:**

- NuttX and FreeRTOS: daily use for embedded system development
- lwIP networking: TCP/UDP socket programming on NuttX and ESP-IDF
- Embedded C: cross-compilation with `arm-none-eabi-gcc`, POSIX API on RTOS
- Docker + QEMU: already set up a working `qemu-armv7a:nsh` development environment for this project
- Certifications: GCP, AWS, TensorFlow Developer, Information Security Specialist (Japan)

GitHub: [https://github.com/wwlapaki310](https://github.com/wwlapaki310)
