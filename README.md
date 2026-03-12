# GSoC 2026 — WireGuard Port to Apache NuttX

> **Google Summer of Code 2026**  
> Organization: [Apache Software Foundation](https://summerofcode.withgoogle.com/programs/2026/organizations/apache-software-foundation)  
> Project: *WireGuard port to NuttX*  
> Difficulty: Major | Size: ~175 hours (Medium)  
> Mentor: Alan Carvalho de Assis (acassis@apache.org)

---

## Project Overview

[Apache NuttX](https://nuttx.apache.org/) is a POSIX-compliant RTOS for microcontrollers.  
This project ports [WireGuard](https://www.wireguard.com/) to NuttX as a **LwIP virtual network interface (netif)**.

### Why not just HTTPS?

HTTPS covers many IoT use cases, but device-side VPN is essential when:

| Scenario | HTTPS | WireGuard |
|----------|-------|-----------|
| Device → Cloud data upload | ✅ sufficient | ✅ |
| Server → Device command push | ⚠️ hard via NAT | ✅ |
| Bidirectional over NAT/firewall | ⚠️ hard | ✅ |
| **Physically unreachable devices** (satellites, space probes, deep-sea sensors) | ❌ | ✅ |
| Firmware update to remote device | ❌ | ✅ |
| Encrypt arbitrary protocols (not just HTTP) | ❌ | ✅ |

For devices that **cannot be physically accessed** — satellites, space hardware, remote sensors —
bidirectional network tunneling is the only maintenance path. WireGuard provides a lightweight,
modern VPN tunnel over UDP, enabling secure two-way communication regardless of NAT or topology.

### Reference Implementations

- [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) — LwIP netif implementation (closest to NuttX)
- [WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) — ESP32 port for reference

---

## Architecture

```
+-----------------------------+
|        NuttX RTOS           |
|                             |
|  Application (NSH, etc.)    |
|          |                  |
|     LwIP TCP/IP Stack       |
|       |         |           |
|   eth0/wlan0   wg0          |  ← WireGuard virtual netif (this project)
|   (physical)  (VPN tunnel)  |
|                  |          |
|      UDP socket (port 51820)|
+-----------------------------+
         |
    Internet / LTE / satellite link
         |
  WireGuard Peer (Linux server)
```

---

## Status

- [x] Repository created
- [x] Feasibility study (`docs/feasibility.md`)
- [x] Why WireGuard analysis (`docs/why-wireguard.md`)
- [ ] Crypto library audit (ChaCha20, Poly1305, Curve25519, BLAKE2s)
- [ ] wireguard-lwip build on NuttX toolchain (QEMU)
- [ ] netif integration with NuttX LwIP
- [ ] NSH command (`wg show`, `wg setconf`)
- [ ] QEMU end-to-end VPN tunnel test
- [ ] Real hardware test (ESP32-S3)

---

## Applicant

**Satoru** — EdgeAI Specialist, Sony Semiconductor Solutions  
Experience: NuttX (SPRESENSE), LwIP, embedded networking, IoT/EdgeAI  
GitHub: https://github.com/wwlapaki310
