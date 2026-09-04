# WireGuard for Apache NuttX

A WireGuard VPN implementation for [Apache NuttX](https://nuttx.apache.org/), exposed as a
`wg0` network device. Verified on real hardware against real WireGuard peers.

> **Discussion:** [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548)
> **Demo:** [youtu.be/1kyX2av5WG4](https://youtu.be/1kyX2av5WG4) — telnet and a web server, both through the tunnel

---

## Status

The tunnel works end to end on hardware, is configurable at runtime, and survives a power
cycle. What remains is upstream submission.

| | |
|---|---|
| **Interface** | `wg0`, registered as a `NET_LL_TUN` netdev over a UDP socket |
| **Configuration** | Runtime (`wg genkey` / `wg set` / `wg setconf`), persisted to a `wg(8)`-compatible INI file |
| **Peers** | 1–16, selectable in Kconfig (~904 bytes of `.bss` per peer) |
| **Throughput** | 260 KiB/s over Wi-Fi on ESP32-S3, tunnelled TCP |
| **Longest observed run** | 4 h 28 min |
| **Upstream** | Not yet submitted — see [Remaining work](#remaining-work) |

### Verified on

| Environment | Architecture | Peer | What was confirmed |
|---|---|---|---|
| sim | x86_64 / Linux | Linux kernel WireGuard | Handshake, traffic, runtime config, **two simultaneous peer sessions** (scripted) |
| QEMU | ARM Cortex-A7 | Linux kernel WireGuard | Traffic on a real NuttX scheduler |
| **ESP32-S3** | Xtensa LX7 | **Windows official client** | **Real Wi-Fi**: telnet, HTTP, 7 MB transfer, rekey, recovery from power loss |
| Spresense | ARM Cortex-M4F | — | `wg0` comes up (**zero code changes**) |

Peers are always real WireGuard implementations — the Linux kernel module and the official
Windows client. Interoperating with another copy of this code would prove nothing.

The port also builds unchanged against both NuttX 12.7.0 and `master`.

---

## Why

NuttX has no VPN. Reaching a device after it has been deployed means exposing a global IP,
building a bespoke protocol, or accepting a vendor cloud — each unappealing for its own
reasons. WireGuard is roughly 4,000 lines, so it fits on a microcontroller, and the peer on
the other end can be any existing WireGuard endpoint.

This matters in places where NuttX already runs:

- **Edge AI and industrial IoT** — firmware updates and diagnostics without physical access
- **Satellites** — after launch, the network is the only maintenance path
- **Unmanned infrastructure** — buoys, mountain stations, pipelines
- **Device-to-device** — an encrypted tunnel without a cloud relay in the middle

---

## Architecture

```
+-----------------------------------------------+
|                  NuttX RTOS                   |
|                                               |
|  Application / NSH                            |
|           |                                   |
|    NuttX Network Stack (BSD socket API)       |
|       |              |                        |
|  eth0 / wlan0       wg0  <- this project      |
|  (physical NIC)  (WireGuard netdev)           |
|                      |                        |
|         +------------+------------+           |
|         |            |            |           |
|   wireguard.c   nuttx-wireguardif.c   nuttx-  |
|   + crypto/     (netdev + UDP socket) platform.c
|   unmodified    written for NuttX     (clock, |
|                      |                urandom)|
|              UDP socket (port 51820)          |
+-----------------------------------------------+
              |
    Internet / LTE / satellite link
              |
    WireGuard peer (Linux, Windows, ...)
```

NuttX has its own TCP/IP stack and does not use lwIP, so the reference implementation's netif
glue could not be reused — `wg0` is registered with `netdev_register()`, modelled on
`drivers/net/tun.c`. The protocol and crypto sources are carried unmodified.

Transmission goes through `devif_poll()` → encrypt → `psock_sendto()`. Reception runs in a
background task that blocks on `psock_poll()`, decrypts, and injects with `ipv4_input()`.

> The socket is held as a `struct socket` rather than a file descriptor, because NuttX scopes
> descriptors per task group and the transmit path runs on an unrelated worker thread. This
> ties the current implementation to FLAT builds — see
> [Issue #6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6).

---

## Usage

```
nsh> wg genkey
<base64 private key>
nsh> wg set private-key <KEY>
nsh> wg set peer <PUBKEY> endpoint 203.0.113.9:51820 \
                          allowed-ips 10.10.0.1/32 \
                          persistent-keepalive 25
nsh> wg up
nsh> wg show
interface: wg0
  public key: <base64>
  listening port: 51820
peer: <base64>
  endpoint: 203.0.113.9:51820
  latest handshake: 5 seconds ago
  transfer: 1.23 KiB received, 0.45 KiB sent

nsh> wg saveconf          # -> /data/wg0.conf, reloaded at next boot
```

The configuration file uses the same INI layout as `wg(8)` (`[Interface]` / `[Peer]`), so a
desktop WireGuard configuration can be dropped in as-is.

> `CONFIG_LINE_MAX` (`CONFIG_NSH_LINELEN` on 12.7.0) must be at least 160 for runtime
> configuration — a full `wg set peer` line runs to about 134 characters, and NSH silently
> truncates it at the default.

---

## Development environment

Docker-based, with three build targets.

```bash
# sim — primary development environment (fast iteration)
docker build --target sim -t nuttx-wireguard:sim .
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wireguard:sim

# QEMU — RTOS verification
docker build --target qemu -t nuttx-wireguard:qemu .
docker run --rm -it nuttx-wireguard:qemu

# ESP32-S3 — real hardware
docker build --target esp32s3 -t nuttx-wireguard:esp32s3 .
```

Build against a different NuttX revision with `--build-arg NUTTX_REF=<ref>` (default
`nuttx-12.7.0`; `master` is known to build).

See [docs/dev-environment.md](docs/dev-environment.md) and [DEVELOPMENT.md](DEVELOPMENT.md).

### Verification scripts

| Script | Proves |
|---|---|
| `scripts/verify-sim-wg-runtime.sh` | A tunnel configured entirely at runtime reaches a live Linux peer, survives save/restore, and rejects bad input |
| `scripts/verify-sim-wg-multipeer.sh` | Two Linux WireGuard interfaces hold sessions with `wg0` at the same time |

---

## Source layout

Everything lives under [`nuttx_port/apps/netutils/wireguard/`](nuttx_port/apps/netutils/wireguard/),
laid out exactly as it would be submitted to `apache/nuttx-apps`.

| | Lines | Origin |
|---|---:|---|
| `wireguard.c`, `crypto/` | 3,079 | [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip), BSD-3-Clause, **byte-identical to upstream** |
| `nuttx-wireguardif.c` | 2,157 | Written for NuttX |
| `wg_main.c` | 344 | The `wg` NSH command |
| `nuttx-wireguardif.h` | 266 | |
| `nuttx-platform.c` | 186 | The four OS hooks |
| Kconfig, build files | 248 | |

The four OS hooks are all the reference implementation needs from the platform:

| Function | NuttX implementation |
|---|---|
| `wireguard_sys_now()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | `return false` |

Isolating them there is why the port moved across four architectures without code changes.
See [the port's README](nuttx_port/apps/netutils/wireguard/README.md) for which files are
third-party, adapted, or original.

---

## Remaining work

| | Tracking |
|---|---|
| Share the design on `dev@nuttx.apache.org` before opening a PR | [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3) |
| Decide how to handle the FLAT-build restriction | [#6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6) |
| Add the wireguard-lwip copyright notice to `LICENSE` | [#7](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/7) |
| Choose the baseline NuttX version | [#8](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/8) |
| Long-run and failure-mode testing (keepalive, reconnect, MTU edges) | [#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5) |

Coding style (`checkpatch.sh`, `nxstyle`) is clean, the vendored sources are in the tree and
verified against upstream, and the directory is already shaped for a pull request. The plan is
in [docs/upstream-strategy.md](docs/upstream-strategy.md); the `dev@` post is drafted in
[docs/dev-list-proposal.md](docs/dev-list-proposal.md).

Beyond that: IPv6, the ESP32-S3 crypto accelerator, and Raspberry Pi Pico 2 W (which needs a
CYW43439 driver on RP2350 — [#1](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/1),
[#2](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/2)).

---

## Documentation

| | |
|---|---|
| [docs/design.html](docs/design.html) | Design document — figures and tables |
| [docs/slides.html](docs/slides.html) | Presentation deck (27 slides; press `N` for speaker notes) |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Current state, how to build and test |
| [docs/upstream-strategy.md](docs/upstream-strategy.md) | Submission plan |
| [docs/hardware-verification.md](docs/hardware-verification.md) | What each board did and did not do |
| [docs/phase4-log.md](docs/phase4-log.md) | Bring-up log, including the failures |

---

## Presentation

To be presented at the **NuttX International Workshop**, co-located with
[Community Over Code Glasgow 2026](https://communityovercode.org/) (Oct 11–14). CFP submitted.

---

## Reference implementations

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip)** — the codebase
this port is built on. Three layers with very different porting costs:

| File | Role | Porting cost |
|---|---|---|
| `wireguard.c` + `crypto/` | Protocol and crypto | None — portable C, used as-is |
| `wireguard-platform.h` | OS hooks (clock, random, timer) | Low — four functions |
| `wireguardif.c` | lwIP netif glue | Total — replaced by `nuttx-wireguardif.c` |

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino)** — an
earlier port of the same code to ESP32 (FreeRTOS + ESP-IDF), read as a reference for which
OS-specific details tend to need attention.

---

## About

I work as an edge AI engineer at Sony Semiconductor Solutions, using NuttX from the
application side — mostly SPRESENSE and ESP32-based edge AI camera systems. Being able to
reach a NuttX board securely for debugging and maintenance is something I wanted myself.

- NuttX: daily use with SPRESENSE and ESP32
- Embedded C: cross-compilation with `arm-none-eabi-gcc`, POSIX API on RTOS
- Certifications: GCP, AWS, TensorFlow Developer, Registered Information Security Specialist (Japan)

GitHub: [wwlapaki310](https://github.com/wwlapaki310)
