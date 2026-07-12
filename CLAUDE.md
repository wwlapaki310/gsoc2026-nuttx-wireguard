# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a GSoC 2026 proposal and development workspace for porting WireGuard to Apache NuttX as a TUN-based virtual network interface (`wg0`). The NuttX port sources live in `nuttx_port/apps/netutils/wireguard/` and are installed into the NuttX apps tree (`apps/netutils/wireguard/`) at build time, together with the protocol core from wireguard-lwip.

## Development Environment

Two ways to build; both produce identical configuration:

**Native build (Ubuntu 24.04) — fastest:**
```bash
scripts/build.sh sim         # host-executable simulator (main dev target)
scripts/build.sh qemu        # qemu-armv7a:nsh (ARM Cortex-A7)
scripts/build.sh esp32       # esp32-devkitc:wifi -> nuttx.bin
scripts/build.sh spresense   # spresense:rndis   -> nuttx.spk
```
The script clones NuttX 12.7.0 / apps / wireguard-lwip into `$NUTTX_WS` (default `~/nuttx-ws`) on first run, installs the WireGuard sources, applies Kconfig options, and builds. Artifacts land in `$NUTTX_WS/out/<target>/`. Switching targets triggers an automatic distclean; pass `--clean` to force reconfiguration.

Requirements: `kconfig-frontends gcc-arm-none-eabi genromfs xxd` (apt), `kconfiglib esptool` (pip), and for ESP32 the xtensa-esp32-elf toolchain in PATH or `$NUTTX_WS/toolchains/xtensa-esp32-elf`.

**Docker build:**
```bash
docker build --target sim       -t nuttx-wg:sim .
docker build --target qemu      -t nuttx-wg:qemu .
docker build --target esp32     -t nuttx-wg:esp32 .
docker build --target spresense -t nuttx-wg:spresense .
```

**Run the sim binary and smoke-test WireGuard:**
```bash
~/nuttx-ws/out/sim/nuttx
# nsh> wg genkey
# nsh> wg up -k <key> -a 10.10.0.2
# nsh> ifconfig            <- wg0 appears
# nsh> wg down
```

**Apply a Kconfig option without menuconfig (inside $NUTTX_WS/nuttx):**
```bash
kconfig-tweak --enable CONFIG_OPTION_NAME
make olddefconfig
```

## Repository Layout

```
.
├── Dockerfile                  # Multi-stage: sim / qemu / esp32 / spresense
├── docker/                     # Entrypoints (sim boots nuttx, qemu launches qemu-system-arm)
├── scripts/build.sh            # Native build for all targets
├── nuttx_port/apps/netutils/wireguard/   # The NuttX port (see below)
└── docs/
    ├── build-and-run.md        # Per-target build/flash/run instructions (Japanese)
    ├── proposal.md             # GSoC proposal text
    └── phase1-log.md           # Build-integration log
```

## Architecture

The protocol/crypto engine (`wireguard.c`, `crypto/`) from [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) is used unmodified; minimal shim headers under `lwip/` satisfy its includes. The lwIP-specific `wireguardif.c` is **not** compiled — it is replaced by `nuttx-wireguardif.c`:

| Upstream (lwIP) | NuttX port |
|---|---|
| `netif_add()` / `netif->output` | TUN char device `/dev/tun` → interface `wg0` |
| `udp_new/bind/recv` | BSD UDP socket |
| `pbuf` | flat static buffers |
| `sys_timeout()` | `poll()` timeout in daemon loop |

Files in `nuttx_port/apps/netutils/wireguard/`:
- `nuttx-platform.c` — `wireguard_sys_now` (CLOCK_MONOTONIC), `wireguard_random_bytes` (/dev/urandom), `wireguard_tai64n_now` (CLOCK_REALTIME), `wireguard_is_under_load` (false)
- `nuttx-wireguardif.{c,h}` — TUN + UDP daemon; public `wireguardif_*` API
- `wg_main.c` — NSH `wg` command (genkey/pubkey/up/peer/status/down)
- `lwip/*.h` — compatibility shims (ip_addr_t = struct in_addr, etc.)

**Critical design point:** the WireGuard daemon runs as a separate task (`task_create`), not a pthread of the `wg` command. In NuttX, open file descriptors belong to the task group — if the transient `wg` command opened the TUN fd, the interface would be destroyed when the command exits. All fds are opened inside the daemon task. State is shared through globals (FLAT build assumption).

## NuttX Build System Conventions

`apps/netutils/wireguard/` uses `Makefile` (+ `Make.defs`, `CMakeLists.txt`) with `MAINSRC = wg_main.c`, `PROGNAME = wg`. `Kconfig`: `CONFIG_NET_WIREGUARD` depends on `NET && NET_UDP && NET_IPv4 && NET_TUN` and `DEV_URANDOM || DEV_RANDOM`.

Required configs (applied by build.sh / Dockerfile):
`CONFIG_ALLOW_BSD_COMPONENTS` (NET_TUN dependency), `CONFIG_NET_TUN`, `CONFIG_NET_TUN_PKTSIZE=1500` (default 296 is smaller than the WireGuard MTU of 1420), `CONFIG_DEV_URANDOM`, `CONFIG_NET_WIREGUARD`, `CONFIG_NSH_LINELEN=160` (base64 keys exceed the default 80-char line).

Target gotchas:
- Spresense: use `spresense:rndis` (native IP stack). The `wifi`/LTE configs are usrsock-based and cannot host TUN.
- ESP32: `esp32-devkitc:wifi` config; iperf/argtable3/cJSON are disabled because they download tarballs at build time.

## Key Files in wireguard-lwip (read in this order)

1. `wireguardif.h` — upstream external API (replaced by `nuttx-wireguardif.h`)
2. `wireguardif.c` — lwIP netif registration (replaced by `nuttx-wireguardif.c`; port reference)
3. `wireguard.c` — handshake and packet processing (used as-is)
4. `crypto/` — cryptographic primitives (no OS dependencies, used as-is)
