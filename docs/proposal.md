# GSoC 2026 Proposal: WireGuard Port to NuttX

## Project Information

| | |
|---|---|
| **Project** | WireGuard port to NuttX |
| **Organization** | Apache Software Foundation |
| **Difficulty** | Major |
| **Size** | ~175 hours (Medium) |
| **Mentors** | Alan Carvalho de Assis (acassis@apache.org), dev@nuttx.apache.org |
| **Reference implementations** | [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip), [ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) |

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

---

## Technical Approach

The implementation follows the architecture of wireguard-lwip:

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

The key integration point is `wireguardif.c`, which registers WireGuard as a lwIP netif via `netif_add()`. Since NuttX uses the same lwIP API, the lwIP integration layer should port with minimal changes. The main work lies in implementing the platform abstraction layer (`wireguard-platform.h`) for NuttX.

---

## Project Timeline

### Phase 1 — Build System Integration (Weeks 1–2)

**Goal:** Get wireguard-lwip source files compiling under the NuttX cross-compilation toolchain. No linking or runtime behavior required yet.

**Tasks:**
- Place wireguard-lwip sources under `apps/netutils/wireguard/`
- Write `CMakeLists.txt` and `Make.defs` following the NuttX convention (referencing `apps/netutils/webserver/` as a model)
- Resolve compiler errors from `arm-none-eabi-gcc` (type definition differences, `__attribute__` usage, etc.)
- Add `Kconfig` entry: `CONFIG_NET_WIREGUARD`

**Deliverable:** Zero build errors when wireguard is included in a `sim:nsh` build.

---

### Phase 2 — NuttX Platform Layer (Weeks 3–4)

**Goal:** Implement `wireguard-platform.h` for NuttX. This is the platform abstraction that wireguard-lwip requires every port to provide.

The four functions to implement:

| Function | Purpose | NuttX implementation |
|----------|---------|---------------------|
| `wireguard_sys_now()` | Monotonic millisecond counter (keepalive/expiry timers) | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | Cryptographic random (key generation, cookies) | `open("/dev/urandom")` + `read()` |
| `wireguard_tai64n_now()` | TAI64N timestamp (replay attack prevention) | `clock_gettime(CLOCK_REALTIME)` or monotonic workaround |
| `wireguard_is_under_load()` | Cookie reply decision | `return false` (sufficient for embedded use) |

**Tasks:**
- Create `nuttx-platform.c` with the four platform functions
- Replace ESP-specific logging (`ESP_LOGI` etc.) in `wireguardif.c` with `syslog()`
- Remove ESP-specific headers (`esp_netif.h`, `tcpip_adapter.h`)
- Verify `CONFIG_DEV_RANDOM` and `CONFIG_CLOCK_MONOTONIC` work on `qemu-armv7a`

**Deliverable:** `wireguardif_init()` runs on QEMU without crashing.

---

### Phase 3 — lwIP netif Registration (Weeks 5–6)

**Goal:** Register WireGuard as a virtual network interface in NuttX's lwIP stack so that `wg0` appears in `ifconfig`.

**Tasks:**
- Call `wireguardif_init()` during NuttX startup (via `nsh_netinit` or a dedicated task)
- Configure WireGuard parameters (private key, listen port) via `Kconfig`
- Verify `netif_add()` succeeds and `wg0` is listed

**Deliverable:** `nsh> ifconfig` shows `wg0`.

---

### Phase 4 — Handshake and Tunnel (Weeks 7–9) ★ Midterm

**Goal:** Complete a WireGuard handshake with a Linux peer and pass traffic through the encrypted tunnel.

**Tasks:**
- Generate key pairs on both NuttX and Linux sides
- Configure peer public key and endpoint in NuttX via Kconfig
- Verify Noise protocol handshake completes over UDP port 51820
- Test end-to-end: `nsh> ping 10.0.0.1` reaches the Linux WireGuard peer

**Deliverable (Midterm):**
```
# On Linux:
$ sudo wg show
peer: <NuttX public key>
  latest handshake: 3 seconds ago
  transfer: 1.20 KiB received, 0.48 KiB sent

# On NuttX (QEMU):
nsh> ping 10.0.0.1
PING 10.0.0.1 56 bytes of data
64 bytes from 10.0.0.1: icmp_seq=0 time=4 ms
```

---

### Phase 5 — NSH Command and Kconfig Integration (Weeks 10–11)

**Goal:** Add a `wg` shell command to NSH for runtime status inspection and configuration.

**Tasks:**
- Implement `wg show` and `wg setconf` as NSH built-in commands
- Register the command in `apps/system/` following NSH conventions
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

**Tasks:**
- Test on ESP32-S3 (Wi-Fi netif + WireGuard netif coexistence)
- Measure actual Flash and RAM usage
- Sign Apache CLA
- Submit PR to `apps/netutils/wireguard/` conforming to NuttX coding style

**Deliverable:** PR open on `apache/nuttx-apps`.

---

## About Me

I work as an EdgeAI engineer at Sony Semiconductor Solutions, where I develop embedded AI camera systems using NuttX-based boards, primarily SPRESENSE and ESP32. My work spans the full stack from hardware bring-up and RTOS configuration to cloud integration and AI inference at the edge.

Being able to establish a secure, bidirectional connection to a NuttX device remotely — for firmware updates, diagnostics, or command execution — is something I have needed in my own work. This project directly addresses that gap.

**Relevant experience:**

- NuttX and FreeRTOS: daily use for embedded system development
- lwIP networking: TCP/UDP socket programming on NuttX and ESP-IDF
- Embedded C: cross-compilation with `arm-none-eabi-gcc`, POSIX API on RTOS
- Docker + QEMU: already set up a working `qemu-armv7a:nsh` development environment for this project (see repository)
- Open source: contributions to AITRIOS platform documentation; active in embedded OSS community

**Certifications:** GCP, AWS, TensorFlow Developer, Information Security Specialist (Japan)

GitHub: [https://github.com/wwlapaki310](https://github.com/wwlapaki310)
