# Reproducing the in-kernel WireGuard build and tests

How to build and test the **in-kernel** version from pinned revisions, without relying on a
pre-existing container or on scripts that happen to already sit in `/tmp` inside it. For the
apps (FLAT) version, use the Dockerfile targets in the [README](../../README.md#development-environment)
instead — note that **the `Dockerfile` currently copies the apps version**
(`nuttx_port/apps/netutils/wireguard/` → `/opt/apps/netutils/wireguard/`); it has no in-kernel
target yet.

## Publication Status

The original development branches remain local. Signed submission branches are
published in the owner's forks, and this coordination repository also carries
a [complete submission patch series](patches/2026-09-29/README.md).

| Fork | Branch | Base (upstream/master) | HEAD | Public? |
|---|---|---|---|---|
| `apache/nuttx` fork | `net-wireguard` | `c95c546c0993…` | `b230ee4876` | local development branch |
| `apache/nuttx-apps` fork | `system-wg` | `73a9c9a69711…` | `37f04cff` | local development branch |
| NuttX submission worktree | `review/pr-series-20260928` | `68dd87f4df` | `c0ead14d83` | fork branches `wireguard-crypto`, `wireguard-driver` |
| apps submission worktree | `review/pr-series-20260928` | `b66303e2` | `e4f910dc18` | fork branch `wireguard-wg` |

Patch application was checked on clean upstream-base worktrees: the resulting
trees match the submission candidates exactly. This is not a claim that a fresh
Docker image build was repeated. DCO certification and fork publication are
complete; opening the Apache PRs remains the owner's action. Original
development branches were not rewritten.

## Prerequisites

- Docker, and a host that can create a TAP device (`/dev/net/tun`, `--cap-add=NET_ADMIN`) and a
  Linux WireGuard interface (`ip link add … type wireguard`, i.e. `wireguard` kernel module +
  `wireguard-tools`) — the tests peer the build against a real Linux kernel WireGuard.
- For the **kernel-build runtime** (T6): a RISC-V bare-metal toolchain and a RISC-V QEMU —
  on Debian/Ubuntu, `gcc-riscv64-unknown-elf` and `qemu-system-misc` (provides
  `qemu-system-riscv64`). The sim build/tests do not need these.

## 1. Get the sources at the pinned revisions

```bash
# Run in a clean working directory; set this to the coordination repo clone.
REVIEW_REPO=/absolute/path/to/gsoc2026-nuttx-wireguard
git clone https://github.com/apache/nuttx.git       nuttx
git clone https://github.com/apache/nuttx-apps.git  apps
git -C nuttx checkout -b wg-candidate 68dd87f4df9ad1e19240136b931867efbcbc23d0
git -C apps  checkout -b wg-candidate b66303e26aa537dd74d6abaeeeded81c151a7e35
git -C nuttx am "$REVIEW_REPO"/docs/upstream/patches/2026-09-29/nuttx/*.patch
git -C apps  am "$REVIEW_REPO"/docs/upstream/patches/2026-09-29/apps/*.patch
```

This repository also carries a dev-loop helper, [`scripts/kdev.sh`](../../scripts/kdev.sh),
that builds/tests a container containing `/opt/nuttx` and `/opt/apps`. Select it
with `WG_CONTAINER` (default `wgdev`). On a Linux host, the patched trees above
can be mounted over those paths in an existing toolchain image:

```bash
docker run -d --name wg-candidate --cap-add=NET_ADMIN --device=/dev/net/tun \
  -v "$PWD/nuttx:/opt/nuttx" -v "$PWD/apps:/opt/apps" \
  --entrypoint sleep nuttx-wireguard:sim-master infinity
export WG_CONTAINER=wg-candidate
cd "$REVIEW_REPO"
# Do not run sync: these source trees already include the complete patches.
```

For the separate local-fork development loop, `kdev.sh sync` applies each fork's
working diff onto a container checked out at the matching **base**, not at a
candidate tip. It now refuses a mismatched HEAD before resetting any source.
Never use an old-base `wgdev` to validate a new-base diff. The image's legacy
apps WireGuard tree must not be mixed with the patched `/opt/apps` tree.

Before publishing the three commits, validate their count, base ranges and
patch form in a container holding the candidate trees:

```bash
bash scripts/kernel/verify-pr-series.sh
# After the author has reviewed and signed all commits:
bash scripts/kernel/verify-pr-series.sh --require-signoff
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
bash scripts/kdev.sh test kernel/verify-sim-wg-downup.sh 150      # down/up lifecycle under flood

# TV: the ChaCha20-Poly1305 u64-counter KAT (needs the .c copied in too)
docker cp scripts/kernel/chachapoly_kat.c wgdev:/tmp/
bash scripts/kdev.sh test kernel/verify-sim-wg-kat.sh 20
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
