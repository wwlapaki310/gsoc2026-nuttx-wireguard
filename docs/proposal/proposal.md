# WireGuard Port to Apache NuttX
**(project: Apache Software Foundation)**

**Satoru Akita**
created: 3/31/2026

---

## 1. Applicant Information

| | |
|---|---|
| 👤 **Name** | Satoru Akita |
| 📧 **Email (main)** | wwlap24@gmail.com |
| 📧 **Email (sub)** | Satoru.Akita@sony.com |
| 💬 **Discord** | fox_aki310 |
| 🐙 **GitHub** | [@wwlapaki310](https://github.com/wwlapaki310) |
| 📁 **Proposal repo** | [wwlapaki310/gsoc2026-nuttx-wireguard](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard) |
| 🗨️ **NuttX discussion** | [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548) |
| 🌏 **Timezone** | JST (UTC+9), Japan |

---

## 2. Project Overview

Apache NuttX is a POSIX-compliant Real-Time Operating System (RTOS) designed for resource-constrained environments. It powers critical embedded systems ranging from PX4-based UAVs to nanosatellites utilizing Sony SPRESENSE, and is also employed in Edge AI cameras running on ESP32. As these devices are increasingly deployed in remote or untrusted networks, secure and encrypted tunnels for firmware updates, diagnostics, and remote maintenance have become a critical requirement. However, a significant gap exists in the current NuttX networking stack: the lack of a native, lightweight VPN capability.

To address this, this GSoC 2026 project aims to port WireGuard to Apache NuttX. WireGuard is a modern, high-performance VPN that utilizes state-of-the-art cryptography — Curve25519, ChaCha20-Poly1305, and BLAKE2s — while maintaining a minimal codebase. Unlike traditional solutions, WireGuard is designed to be as simple to configure as SSH and highly efficient on microcontrollers, as proven by its successful deployment in OpenWrt and ESP32 environments since its integration into the Linux kernel 5.6 (2020).

By bringing WireGuard to Apache NuttX, this project will empower developers to deploy secure-by-default, production-ready communication in embedded systems with minimal resource overhead, bridging the gap between existing RTOS capabilities and modern security standards.

Contributions to the Apache NuttX community:

- **Adds a missing capability.** This brings the first standard VPN solution to NuttX.

- **Available to all NuttX developers immediately.** The goal is to upstream the code to `apache/nuttx-apps`, so any NuttX developer can enable WireGuard with `CONFIG_NET_WIREGUARD=y` without doing their own porting work.

- **Establishes a pattern for porting lwIP-based libraries to NuttX.** Many embedded environments (FreeRTOS, ESP-IDF) use lwIP as their networking library, and a large body of lwIP-based software exists — including Mongoose Web Server (embedded HTTP/WebSocket), libcoap (IoT CoAP), and Eclipse Paho Embedded MQTT. Since NuttX has its own network stack, there has been no clear path to run these on NuttX. This project demonstrates and documents the bridging approach, serving as a reference for future porters beyond WireGuard.

The project results will be presented at the **NuttX International Workshop at Community Over Code Glasgow** (October 11–14, 2026). CFP has already been submitted.

---

## 3. Proposal Details

| | |
|---|---|
| **Organization** | [Apache Software Foundation](https://summerofcode.withgoogle.com/programs/2026/organizations/apache-software-foundation) |
| **Difficulty** | Major |
| **Size** | ~175 hours (Medium) |
| **Mentor** | Alan Carvalho de Assis (acassis@apache.org), dev@nuttx.apache.org |

### 3.1 Reference Projects

Two existing projects are used as references, each serving a different role.

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) — the source code to port**

This is the actual code being brought into NuttX. It implements WireGuard as a lwIP netif (network interface). A netif is lwIP's abstraction for a virtual NIC — a network interface unit treated on equal footing with `eth0` or `wlan0`. By implementing WireGuard as a netif, it appears to the upper network stack as a regular NIC, and routing and packet forwarding work transparently.

All OS-specific behavior is isolated behind a platform abstraction layer (`wireguard-platform.h`) consisting of four functions. The WireGuard protocol core (`wireguard.c`) and cryptographic primitives (`crypto/`) are written in portable C with no OS or architecture dependencies and can be used as-is. The porting work has two parts: implementing `wireguard-platform.h` for NuttX (replacing time, random, and timer APIs), and replacing the lwIP-specific API calls in `wireguardif.c` with NuttX equivalents (`struct netif` → `struct net_driver_s`, `pbuf_alloc()` → `iob_alloc()`, `udp_new()/bind()` → BSD `socket()/bind()`, etc.).

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) — a prior port**

A port of wireguard-lwip to ESP32 running FreeRTOS + ESP-IDF. Since ESP-IDF uses lwIP, this port can use `wireguardif.c` more directly. It is used as a reference for the OS-specific platform layer — specifically timer APIs, random number generation, and mutex patterns in an embedded RTOS.

**How this project differs:** NuttX does not use lwIP. NuttX has its own TCP/IP stack, and lwIP public headers such as `lwip/netif.h` are not in the include path. This means `wireguardif.c` cannot be compiled on NuttX as-is. The contribution of this project is demonstrating and documenting how to connect a lwIP-based network component to NuttX's native netdev and socket API — a reusable pattern for the community.

---

### 3.2 Task List

**Phase 0–1 (completed pre-application)**
- Docker development environment with two build targets (`sim` with TUN/TAP networking and `qemu-armv7a`)
- Build system files for `apps/netutils/wireguard/` (`CMakeLists.txt`, `Make.defs`, `Kconfig`)
- Minimal lwIP compatibility shim headers so `wireguard.c` compiles without modification
- Full API mapping: all lwIP calls in `wireguardif.c` mapped to NuttX equivalents
- ✅ `CONFIG_NET_WIREGUARD=y` builds successfully; NuttX boots to `nsh>` on SIM

**Phase 2 — NuttX Integration Layer on SIM (Jun 16 – Jul 4)**
- `nuttx-platform.c`: implement four OS-specific functions using NuttX POSIX APIs (time, random, timer)
- `nuttx-wireguardif.c`: replace lwIP APIs (`struct netif`, `pbuf`, `udp_*`) with NuttX `net_driver_s`, `iob`, and BSD sockets
- ✅ `nsh> ifconfig` shows `wg0` alongside `eth0` on SIM

**Phase 3 — Handshake and Tunnel on QEMU (Jul 14 – Aug 8)**
- Port the working SIM configuration to `qemu-armv7a` (verify behavior under NuttX's own scheduler)
- Establish WireGuard handshake with a Linux peer
- ✅ `nsh> ping 10.0.0.1` succeeds; Linux-side `wg show` confirms handshake

**Phase 4 — NSH Command and ESP32-S3 Hardware (Aug 18 – Sep 12)**
- Implement `wg show` / `wg setconf` as NSH built-in commands
- Establish WireGuard tunnel on ESP32-S3 hardware over Wi-Fi
- ✅ End-to-end tunnel working on real hardware; Flash/RAM usage measured

**Phase 5 — Upstream PR (Sep 15 – Sep 30)**
- Sign Apache CLA
- Submit PR to `apps/netutils/wireguard/` in `apache/nuttx-apps`
- ✅ PR open on `apache/nuttx-apps`

---

### 3.3 Timeline

As a full-time working professional, I plan to contribute approximately 12–15 hours per week with a 2-week buffer built into the schedule.

| Period | Dates | Content |
|--------|-------|---------|
| Community Bonding | May 1 – May 24 | Phase 0: finalize API mapping, align approach with mentor |
| Phase 1 | ✅ completed pre-application | Build system integration |
| Weeks 1–5 | May 25 – Jun 27 | Phase 2: NuttX integration layer (SIM) |
| Buffer | Jun 30 – Jul 4 | Catch-up / investigation |
| ★ Midterm evaluation | Jul 6 – Jul 10 | Phase 2 deliverable: `wg0` visible in `ifconfig` |
| Weeks 7–10 | Jul 14 – Aug 8 | Phase 3: QEMU handshake and tunnel |
| (Obon holiday) | Aug 8 – Aug 15 | Unavailable |
| Weeks 11–14 | Aug 18 – Sep 12 | Phase 4: NSH command + ESP32-S3 hardware |
| Weeks 15–18 | Sep 15 – Sep 30 | Phase 5: upstream PR |
| Conference prep | Oct 1 – Oct 10 | ASF Conference preparation |
| Conference | Oct 11 – Oct 14 | ASF Conference @ Glasgow (CFP submitted) |

---

## 4. Communication

Timezone: UTC+9 (Japan Standard Time)

### 4.1 Communication Preferences

From my experience in remote and hybrid work, I am comfortable with asynchronous communication across time zones. At work I primarily use Microsoft Teams; outside of work I regularly use Discord, Slack, Google Meet, Zoom, and X (Twitter) depending on context. I am flexible with whatever tools the mentor and community prefer.

For this project, my planned approach is:

- **GitHub Issues / PR comments:** The primary venue for technical discussion and code review, kept open so the whole community can follow along.
- **Discord:** Quick questions and synchronous check-ins. I would like to propose creating a dedicated Discord channel for mentor communication.
- **Weekly progress reports:** Posted each week in the Discord channel or on GitHub, covering both progress and any technical blockers — shared early to prevent the project from stalling.

| Channel | Purpose |
|---------|---------|
| 📧 Email (wwlap24@gmail.com) | Urgent contact and official notifications; reply within 24 hours |
| 💬 Discord (fox_aki310) | Day-to-day communication, weekly sync, progress reports |
| 🗨️ GitHub Issues / PRs | Technical discussion, code review, task tracking |
| 📋 NuttX mailing list | Important community-wide updates and discussion |

### 4.2 English Proficiency

English is my second language, but text-based communication (chat, email, code review) is not a problem. I am working on improving my spoken English daily. I have given presentations in English at the SXSW hackathon and for my master's thesis defense. I also collaborated with the Sony Europa team (Lund, Sweden) to develop AITRIOS sample applications and publish them as OSS on GitHub.

---

## 5. About Me

| | |
|---|---|
| 📄 **CV** | [Google Drive](https://drive.google.com/file/d/1WaaCUJOFb_DxdXQ1hG7ZQF_cu7Jbm_pr/view) |
| 💼 **LinkedIn** | [satoru-akita-6070a4145](https://www.linkedin.com/in/satoru-akita-6070a4145/) |
| 🏢 **Employer** | [Sony Semiconductor Solutions Corporation](https://www.sony-semicon.com/en/index.html) |
| 🎓 **Education** | M.S. in Robotics, [Tohoku University](http://www.mems.mech.tohoku.ac.jp/index_e.html) |
| 📝 **Blog** | [wwlapaki310.github.io](https://wwlapaki310.github.io/) |

### 5.1 Motivation

I work at Sony Semiconductor Solutions on two product areas where NuttX is deployed in production.

**AITRIOS (Edge AI Camera Platform)**

A platform centered around edge AI cameras combining the Sony IMX500 intelligent vision sensor with ESP32, which runs NuttX. These cameras are deployed in logistics warehouses, retail stores, and traffic monitoring systems — running in the field for extended periods.
Reference: https://www.aitrios.sony-semicon.com/edge-ai-devices

**SPRESENSE (Low-Power Microcontroller)**

A compact, low-power microcontroller board developed by Sony, with adoption in mission-critical applications including satellites and ocean monitoring. The lunar transforming robot SORA-Q (launched 2023) uses SPRESENSE, and small satellite projects in partnership with JAXA (Japan Aerospace Exploration Agency) are ongoing.
Reference: https://www.hackster.io/news/sora-q-the-sony-spresense-powered-transforming-robot-heads-moonward-if-spacex-can-fix-falcon-9-c81e490e78b1

---

Working on these projects, I have repeatedly run into the need to securely access a NuttX device in the field — firmware updates for cameras deployed in warehouses, remote debugging of satellites — all of which require secure communication that does not assume physical access. WireGuard solves this problem on Linux. That is the direct motivation for wanting to make it work on NuttX.

As a daily NuttX user, I also want to make a concrete contribution to the open-source community. I have previously published AITRIOS sample applications as OSS, but contributing to an existing OSS project upstream will be a first for me. I want to build that experience and become an engineer who contributes to the global software ecosystem.

### 5.2 Availability

- **Hours:** ~12–15 hours per week (weekday evenings + weekends, JST)
- **Unavailable:** August 8–15 (Obon holiday, Japan)
- **Submission target:** End of September. Will aim for the August 25 standard deadline if ahead of schedule.
- **Post-GSoC:** Will present at ASF Conference @ Glasgow.

### 5.3 Background and Interests

I regularly attend software conferences, help with community organizing, and participate in hackathons. I served as a staff organizer at Open Source Summit Japan, and took part in a robotics hackathon using the Unitree G1.

![Open Source Summit Japan (organizer)](../../assets/oss-summit-japan.jpg)
![Unitree G1 Robot Hackathon](../../assets/unitree-g1-hackathon.jpg)

I grew up fascinated by space and spent my student years building rockets and autonomous robots — an interest that eventually led me to edge AI and spacecraft development with SPRESENSE and NuttX. Outside of tech, I enjoy marathon running, golf, travel, and history.

**Technical skills relevant to this project:**

- **Embedded C:** Daily use of `arm-none-eabi-gcc` cross-compilation, POSIX API on RTOS, register-level hardware work
- **NuttX:** Application-side development on SPRESENSE and ESP32; familiar with the build system (Kconfig, CMake, make), NSH, and the netdev/socket API
- **Networking:** TCP/IP stack fundamentals, BSD socket API, UDP, VPN concepts
- **Docker + QEMU:** Built the `sim` and `qemu-armv7a` development environments for this project from scratch
- **C proficiency:** Comfortable reading and modifying low-level C in embedded contexts; read through wireguard-lwip and WireGuard-ESP32-Arduino codebases as part of proposal preparation

**Certifications:** GCP Professional, AWS Certified, TensorFlow Developer Certificate, Information Security Specialist (IPA, Japan)

**Selected achievements:**
- SPAJAM 2024 Excellence Award (national hackathon finals, NHK broadcast)
- Harvard BIOMD 2015 Grand Prize
- NASA Space Apps Challenge 2020 Tokyo Winner
- ICAN 2014 World 3rd Place

### 5.4 About the Community Over Code Glasgow Submission

I had the opportunity to speak with Jerpelea Alin through the Sony Group internal chat, and his encouragement helped me move forward with this application.

As a planned venue for sharing the results of this project, I have submitted a CFP to the NuttX International Workshop at Community Over Code Glasgow (October 11–14, 2026).

- CFP: [docs/proposal for ASF2026.md](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/blob/main/docs/proposal%20for%20ASF2026.md)

I don't yet know whether it will be accepted, but I am looking forward to attending and meeting everyone in Glasgow.
