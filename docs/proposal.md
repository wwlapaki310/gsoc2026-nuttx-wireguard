# WireGuard Port to Apache NuttX

---

## Name and Contact Information

| | |
|---|---|
| 👤 **Name** | Satoru Akita |
| 📧 **Email** | wwlap24@gmail.com |
| 🐙 **GitHub** | [@wwlapaki310](https://github.com/wwlapaki310) |
| 📁 **Proposal repo** | [wwlapaki310/gsoc2026-nuttx-wireguard](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard) |
| 💬 **NuttX discussion** | [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548) |
| 🌏 **Timezone** | JST (UTC+9), Japan |

---

## Title

WireGuard Port to Apache NuttX

---

## Synopsis

NuttX is used in satellites, edge AI cameras, robotics, and remote sensing systems — devices where secure remote access is critical but currently impossible. This project ports WireGuard to NuttX, implementing it as a native network device (`wg0`) so that any NuttX device can establish an encrypted VPN tunnel using standard WireGuard tooling on the peer side. The primary reference is wireguard-lwip (smartalock), whose portable protocol core is reused as-is, while the network integration layer is rewritten for NuttX's netdev API and BSD socket interface.

I have already completed Phase 1 before the application deadline: the build system is integrated, Docker environments for SIM and QEMU are working, and the key architectural finding — that NuttX's TCP/IP stack is independent from lwIP, requiring a new `nuttx-wireguardif.c` — is documented. The remaining work fits clearly within the 175-hour scope.

---

## Benefits to Community

NuttX runs in environments where physical access is impractical or impossible: small satellites, ocean buoys, edge AI cameras deployed in the field, unmanned infrastructure. Firmware updates, remote debugging, and secure device-to-device communication all require a VPN that is small enough to run on a microcontroller and simple enough to configure with a static key pair.

WireGuard is the right answer for this. Its codebase is small, its cryptography is modern, and its key model is simple. It is already shipping on Linux devices at scale. Porting it to NuttX brings that same capability to the embedded RTOS world.

For the Apache NuttX community specifically:

- **A gap in the ecosystem is filled.** NuttX has no VPN support today. This adds a capability that many users have been working around manually.
- **The implementation will be upstreamed.** The goal is a PR to `apache/nuttx-apps` that any NuttX user can enable with `CONFIG_NET_WIREGUARD=y`.
- **The approach is a reference.** The `nuttx-wireguardif.c` integration layer demonstrates how to port a lwIP-based network component to NuttX's native netdev API, which will help future porting efforts.

The project will be presented at the **NuttX International Workshop at Community Over Code Glasgow** (October 11-14, 2026). CFP has been submitted.

---

## Related Work

**smartalock/wireguard-lwip** (https://github.com/smartalock/wireguard-lwip)

This is the primary reference and the source of the portable code used in this project. It implements WireGuard as a lwIP netif and isolates all OS-specific behavior behind four functions in `wireguard-platform.h`. The protocol core (`wireguard.c`) and cryptographic primitives (`crypto/`) are written in portable C with no OS or architecture dependencies.

The key finding from Phase 1: NuttX does not use lwIP. NuttX has its own TCP/IP stack (uIP-derived, but largely an original design). This means `wireguardif.c` — the lwIP integration layer — cannot be compiled as-is on NuttX. The solution is `nuttx-wireguardif.c`, a new file that reuses the protocol logic from `wireguardif.c` while replacing the lwIP API calls with NuttX's netdev API and BSD socket API.

**ciniml/WireGuard-ESP32-Arduino** (https://github.com/ciniml/WireGuard-ESP32-Arduino)

A prior port of wireguard-lwip to ESP32 running FreeRTOS + ESP-IDF. Since ESP-IDF uses lwIP, this port can use `wireguardif.c` more directly. It is used as a reference for the OS-specific platform layer (`wireguard-platform.h`) — specifically, the timer API, random number generation, and mutex patterns in an embedded RTOS.

**How this project differs:**

NuttX's network stack is not lwIP, so the integration challenge is more fundamental than the ESP32 port. The contribution of this project is demonstrating and documenting how to connect a lwIP-based network component to NuttX's native netdev and socket API — a reusable pattern for the community beyond WireGuard itself.

---

## Deliverables

### Already completed (pre-application)

- Docker-based development environment with two build targets: `sim` (NuttX running as a Linux process with TUN/TAP networking) and `qemu-armv7a`
- `CONFIG_NET_WIREGUARD=y` builds without errors; NuttX boots to `nsh>` on SIM with `eth0` visible
- Build system files for `apps/netutils/wireguard/`: `CMakeLists.txt`, `Make.defs`, `Makefile`, `Kconfig`
- Minimal lwIP compatibility shim headers (`lwip/netif.h`, `lwip/udp.h`, etc.) so `wireguard.c` compiles without modification
- Stub `nuttx-wireguardif.c` and `nuttx-platform.c` to unblock the build
- API mapping: all lwIP calls in `wireguardif.c` mapped to their NuttX equivalents (documented in `docs/phase1-log.md`)

### Phase 0 — Community Bonding (May 8 – Jun 1)

- Finalize the design of `nuttx-wireguardif.c` based on the API mapping
- Confirm approach with mentor (Alan Carvalho de Assis)

Deliverable: design document; mentor sign-off on approach.

### Phase 1 — Build System Integration ✅ Completed

Deliverable: `CONFIG_NET_WIREGUARD=y` build succeeds; NuttX reaches `nsh>`.

### Phase 2 — NuttX Integration Layer on SIM (Jun 16 – Jul 4) [REQUIRED]

Implement the two remaining files:

**nuttx-platform.c** — four functions replacing OS-specific behavior:

| Function | NuttX implementation |
|----------|---------------------|
| `wireguard_sys_now()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | `return false` |

**nuttx-wireguardif.c** — network integration layer:

| lwIP (wireguardif.c) | NuttX (nuttx-wireguardif.c) |
|----------------------|------------------------------|
| `struct netif` | `struct net_driver_s` |
| `netif_add()` | `netdev_register()` |
| `ip_input(pbuf, netif)` | `devif_input(dev)` |
| `udp_new()` / `udp_bind()` / `udp_recv()` | BSD `socket()` / `bind()` / `recvfrom()` |
| `pbuf_alloc()` / `pbuf_free()` | `iob_alloc()` / `iob_free()` |
| `sys_timeout()` | `wd_start()` |

Deliverable: `nsh> ifconfig` shows `wg0` alongside `eth0` on SIM. [REQUIRED]

### Buffer week (Jul 7 – Jul 11)

Investigation and catch-up. No deliverable.

### Phase 3 — Handshake and Tunnel on QEMU (Jul 14 – Aug 1) ★ Midterm [REQUIRED]

Port the working SIM configuration to `qemu-armv7a`. QEMU introduces an emulated ARM CPU with NuttX's own scheduler, necessary to verify that WireGuard's timer-based operations (keepalive, handshake expiry) work correctly.

- Generate key pairs on NuttX (QEMU) and Linux sides
- Verify Noise protocol handshake over UDP port 51820
- Test end-to-end traffic: `nsh> ping 10.0.0.1`

Deliverable:
```
# Linux:
$ sudo wg show
peer: <NuttX public key>
  latest handshake: 3 seconds ago

# NuttX (QEMU):
nsh> ping 10.0.0.1
64 bytes from 10.0.0.1: icmp_seq=0 time=4 ms
```

### Phase 4 — NSH Command and Real Hardware (Aug 4 – Sep 5) [REQUIRED]

**NSH wg command:**

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

- Implement `wg show` and `wg setconf` as NSH built-in commands
- Finalize Kconfig dependency chain: `NET_WIREGUARD` depends on `NET`, `NET_UDP`, `MBEDTLS`

**ESP32-S3 real hardware test:**

QEMU uses virtio-net; ESP32-S3 uses a Wi-Fi driver. Real hardware validates the netdev integration with a physical interface and provides actual Flash and RAM measurements.

Deliverable: WireGuard tunnel working on ESP32-S3 over Wi-Fi; Flash/RAM measurements recorded. [REQUIRED]

### Phase 5 — Upstream PR and Documentation (Sep 8 – Sep 27) [REQUIRED]

- Sign Apache CLA
- Submit PR to `apache/nuttx-apps` at `apps/netutils/wireguard/` conforming to NuttX coding style
- Address review feedback
- Write documentation

Deliverable: PR open on `apache/nuttx-apps`. [REQUIRED]

### ASF Conference @ Glasgow (Oct 11 – Oct 14)

Present project results at the NuttX International Workshop, co-located with Community Over Code Glasgow 2026. CFP has been submitted.

---

## Timeline Summary

| Period | Dates | Content |
|--------|-------|---------|
| Community Bonding | May 8 – Jun 1 | Phase 0: design finalization |
| Weeks 1–2 | Jun 2 – Jun 13 | Phase 1 ✅ completed pre-GSoC |
| Weeks 3–5 | Jun 16 – Jul 4 | Phase 2: NuttX integration layer |
| Buffer | Jul 7 – Jul 11 | catch-up / investigation |
| Weeks 6–8 | Jul 14 – Aug 1 | Phase 3: QEMU handshake ★ Midterm |
| Weeks 9–11 | Aug 4–8, Aug 18 – Sep 5 | Phase 4: NSH command + ESP32-S3 |
| (Obon holiday) | Aug 8 – Aug 15 | unavailable |
| GSoC final submission | Aug 25 | — |
| Weeks 12–14 | Sep 8 – Sep 27 | Phase 5: upstream PR |
| Conference | Oct 11 – Oct 14 | ASF Conference @ Glasgow |

Availability: approximately 15 hours per week (weekday evenings + weekends, JST).

---

## Biographical Information

I work as an Edge AI engineer at Sony Semiconductor Solutions. My day-to-day work involves the Sony IMX500 intelligent vision sensor and the SPRESENSE and ESP32-based camera systems that run on top of it. I use Apache NuttX from the application side on these platforms regularly.

The motivation for this project is direct: I have repeatedly run into the problem of needing to securely access a NuttX device in the field — for a firmware update, a configuration change, or to pull diagnostic data — and having no clean solution. WireGuard solves exactly this problem on Linux. I want it to work on NuttX too.

**Technical skills relevant to this project:**

- **Embedded C:** daily use of cross-compilation with `arm-none-eabi-gcc`, POSIX API on RTOS, direct register-level hardware work
- **NuttX:** application-side development on SPRESENSE and ESP32; familiar with the build system (Kconfig, CMake, make), NSH, and the netdev/socket API
- **Networking:** TCP/IP stack fundamentals, BSD socket API, UDP, VPN concepts
- **Docker + QEMU:** set up the working `sim` and `qemu-armv7a` development environments for this project from scratch
- **C language proficiency:** comfortable reading and modifying low-level C code in embedded contexts; read through wireguard-lwip and wireguard-ESP32-Arduino codebases as part of proposal preparation

**Certifications:** GCP Professional, AWS Certified, TensorFlow Developer Certificate, Information Security Specialist (Japan National Exam)

**Selected achievements:**

- SPAJAM 2024 Excellence Award (national hackathon finals, NHK broadcast)
- Harvard BIOMD 2015 Grand Prize
- NASA Space Apps Challenge 2020 Tokyo Winner
- ICAN 2014 World 3rd Place

**Communication:** I am reachable by email at wwlap24@gmail.com and active on the NuttX mailing list and the discussion thread at https://github.com/apache/nuttx/issues/18548. I commit to weekly progress updates to the mentor and will be available via email throughout the coding period.
