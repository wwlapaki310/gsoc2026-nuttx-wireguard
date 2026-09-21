# Reproducing the in-kernel WireGuard build and tests

How to build and test the **in-kernel** version from pinned revisions, without relying on a
pre-existing container or on scripts that happen to already sit in `/tmp` inside it. For the
apps (FLAT) version, use the Dockerfile targets in the [README](../../README.md#development-environment)
instead — note that **the `Dockerfile` currently copies the apps version**
(`nuttx_port/apps/netutils/wireguard/` → `/opt/apps/netutils/wireguard/`); it has no in-kernel
target yet.

## ⚠️ Publication status (blocks a clean external repro today)

The in-kernel code lives in two **local** forks that are **not yet pushed**:

| Fork | Branch | Base (upstream/master) | HEAD | Public? |
|---|---|---|---|---|
| `apache/nuttx` fork | `net-wireguard` | `c95c546c0993…` | `677c8f54fe46…` | **no — not pushed (GitHub returns 404)** |
| `apache/nuttx-apps` fork | `system-wg` | `73a9c9a69711…` | `24b3f31157e5…` | **no — not pushed** |

Until those branches are pushed, a third party cannot fetch the driver/command sources, so the
steps below cannot be run from a fresh external clone. **Publishing the forks is the author's
task** (see the role split in [issue #11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11)).
The procedure is written so it works the moment they are public; on the author's machine it
works today because the forks are checked out locally.

## Prerequisites

- Docker, and a host that can create a TAP device (`/dev/net/tun`, `--cap-add=NET_ADMIN`) and a
  Linux WireGuard interface (`ip link add … type wireguard`, i.e. `wireguard` kernel module +
  `wireguard-tools`) — the tests peer the build against a real Linux kernel WireGuard.
- For the **kernel-build runtime** (T6): a RISC-V bare-metal toolchain and a RISC-V QEMU —
  on Debian/Ubuntu, `gcc-riscv64-unknown-elf` and `qemu-system-misc` (provides
  `qemu-system-riscv64`). The sim build/tests do not need these.

## 1. Get the sources at the pinned revisions

```bash
# upstream, at the fork base
git clone https://github.com/apache/nuttx.git       nuttx
git clone https://github.com/apache/nuttx-apps.git  apps
git -C nuttx checkout c95c546c0993
git -C apps  checkout 73a9c9a69711

# the in-kernel changes, once the forks are public (see the caveat above):
#   git -C nuttx remote add fork <nuttx fork url>  && git -C nuttx fetch fork net-wireguard && git -C nuttx checkout 677c8f54fe46
#   git -C apps  remote add fork <apps fork url>   && git -C apps  fetch fork system-wg     && git -C apps  checkout 24b3f31157e5
```

This repository also carries a dev-loop helper, [`scripts/kdev.sh`](../../scripts/kdev.sh),
that syncs each fork's working-tree diff into a container and builds/tests it. It assumes a
container named `wgdev` (from image `nuttx-wireguard:sim-master`, with `/opt/nuttx` and
`/opt/apps` checked out at the fork bases). To create that container from scratch:

```bash
docker build -t nuttx-wireguard:sim-master .            # base image
docker run -d --name wgdev --cap-add=NET_ADMIN --device=/dev/net/tun \
  nuttx-wireguard:sim-master sleep infinity
# then, from a clone of THIS repo with the forks checked out beside it:
bash scripts/kdev.sh sync                                # applies each fork's diff into wgdev
```

## 2. sim: build and the runtime + replay tests (T1, TR)

```bash
bash scripts/kdev.sh configure                           # sim:nsh + NET_WIREGUARD + SYSTEM_WG
bash scripts/kdev.sh build                               # expect BUILD_EXIT=0 and an nuttx binary
bash scripts/kdev.sh test kernel/verify-sim-wg-runtime.sh 40    # T1
bash scripts/kdev.sh test kernel/verify-sim-wg-ioctl.sh 60      # TF (ioctl negatives)
bash scripts/kdev.sh test kernel/verify-sim-wg-replay.sh 40     # TR (replay/cookie)
bash scripts/kdev.sh test kernel/verify-sim-wg-negotiation.sh 80  # TN (negative interop)
bash scripts/kdev.sh test kernel/verify-sim-wg-multipeer.sh 80    # T3 (two peers at once)
```

Expected: each `verify-*` script ends with its `PASS: …` terminal line (they run under
`set -euo pipefail`, so reaching that line means every check passed). `kdev.sh` now propagates a
non-zero exit if the build or a test fails (see [issue #12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12)).

## 3. Kernel build (BUILD_KERNEL)

Copy the two build scripts into the container and run them (they do `distclean` + configure +
build + export/import of the app ELFs):

```bash
docker cp scripts/kernel/build-knsh.sh    wgdev:/tmp/
docker cp scripts/kernel/build-knetnsh.sh wgdev:/tmp/
docker exec wgdev bash /tmp/build-knsh.sh                # qemu-armv7a:knsh — expect KNSH_BUILD_EXIT=0
docker exec wgdev bash /tmp/build-knetnsh.sh             # rv-virt:knetnsh64 — expect KERNEL_BUILD_EXIT=0 and apps/bin/wg
```

`build-knsh.sh` proves the apps/kernel symbol split compiles and links. `build-knetnsh.sh`
additionally produces the `wg` ELF for the runtime tunnel.

## 4. Kernel-build runtime tunnel to a real Linux peer (T6)

```bash
docker cp scripts/kernel/verify-knetnsh-wg.sh wgdev:/tmp/
docker exec wgdev bash /tmp/verify-knetnsh-wg.sh
```

Expected final line:
`PASS: knetnsh64 in-kernel WireGuard tunnel verified (BUILD_KERNEL + virtio-net + real Linux peer)`

QEMU specifics baked into that script (worth knowing if you adapt it): rv-virt:knetnsh64 is an
S-mode build, so it needs OpenSBI (**not** `-bios none`); the console is the 16550 UART via
`-serial mon:stdio`; virtio-net needs `-M virt,aclint=on -global virtio-mmio.force-legacy=false
-device virtio-net-device,bus=virtio-mmio-bus.0`; and NSH drops the first byte after a prompt,
so each command is sent with a leading newline.

## What this does and does not establish

See [verification-matrix.md](verification-matrix.md) for the full, honest status. In short: the
above reproduces **T1, TF, TR, TN, T3, and T6** (protocol/crypto correctness and ioctl/negative
validation on sim against Linux, and a full tunnel in a real kernel build). It does **not** cover
TV (KAT), TZ, TE, TT, T7, or hardware (T5) — those are not yet implemented or not yet run against
the kernel version.
