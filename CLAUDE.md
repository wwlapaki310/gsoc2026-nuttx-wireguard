# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a GSoC 2026 proposal and development workspace for porting WireGuard to Apache NuttX as an lwIP virtual network interface (`wg0`). The repository currently contains the Docker-based development environment and documentation. Actual WireGuard implementation code will live under `apps/netutils/wireguard/` in the NuttX apps tree (inside the Docker container at `/opt/apps/`).

## Development Environment

All NuttX development happens inside Docker. The container clones NuttX 12.7.0 and wireguard-lwip at build time.

**Build the Docker image:**
```bash
docker build -t nuttx-wireguard .
```

**Run NuttX on QEMU (ARM Cortex-A7):**
```bash
docker run --rm -it nuttx-wireguard
# Inside container: QEMU starts automatically via docker-entrypoint.sh
# Exit QEMU: Ctrl-A then X
```

**Shell inside the container (for development):**
```bash
docker run --rm -it --entrypoint bash nuttx-wireguard
```

**Inside the container — rebuild NuttX:**
```bash
cd /opt/nuttx
make -j$(nproc)
```

**Inside the container — reconfigure with menuconfig:**
```bash
cd /opt/nuttx
make menuconfig
make olddefconfig
make -j$(nproc)
```

**Inside the container — apply a Kconfig option without menuconfig:**
```bash
kconfig-tweak --enable CONFIG_OPTION_NAME
make olddefconfig
```

## Repository Layout

```
.
├── Dockerfile                  # Full dev environment: NuttX 12.7.0 + wireguard-lwip + QEMU
├── docker/docker-entrypoint.sh # Launches QEMU with virtio-net + UDP 51820 forwarded
└── docs/
    ├── proposal.md             # GSoC proposal text
    ├── development-roadmap.md  # Phase-by-phase plan (Japanese)
    ├── feasibility.md          # Technical feasibility study (Japanese)
    └── why-wireguard.md
```

Inside the container:
- `/opt/nuttx/` — NuttX kernel source (nuttx-12.7.0)
- `/opt/apps/` — NuttX apps source (nuttx-12.7.0); WireGuard code goes in `netutils/wireguard/`
- `/opt/wireguard-lwip/` — Reference implementation to port

## Architecture

WireGuard is implemented as an lwIP `netif` (virtual NIC). The porting target is [wireguard-lwip](https://github.com/smartalock/wireguard-lwip), which isolates all OS-specific behavior behind `wireguard-platform.h`. The four functions to implement for NuttX:

| Function | NuttX implementation |
|----------|---------------------|
| `wireguard_sys_now()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | `read("/dev/urandom")` — requires `CONFIG_DEV_URANDOM=y` |
| `wireguard_tai64n_now()` | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | `return false` |

NuttX uses POSIX threading (`pthread_create`, `pthread_mutex_t`) — no FreeRTOS primitives. Logging uses `syslog()` instead of `ESP_LOGI`.

## NuttX Build System Conventions

When adding `apps/netutils/wireguard/`:
- `CMakeLists.txt` — CMake build rules (follow existing `apps/netutils/` examples)
- `Make.defs` — Makefile build rules
- `Kconfig` — config entry: `CONFIG_NET_WIREGUARD` depends on `NET && NET_UDP && MBEDTLS`

Enabled configs in the current Docker build: `CONFIG_NET`, `CONFIG_NET_IPv4`, `CONFIG_NET_UDP`, `CONFIG_VIRTIO`, `CONFIG_VIRTIO_NET`, `CONFIG_MBEDTLS`, `CONFIG_DEV_RANDOM`.

## Key Files in wireguard-lwip (read in this order)

1. `wireguardif.h` — external API
2. `wireguardif.c` — lwIP netif registration (core of Phase 3)
3. `wireguard.c` — handshake and packet processing (core of Phase 4)
4. `crypto/` — cryptographic primitives (no OS dependencies, use as-is)
