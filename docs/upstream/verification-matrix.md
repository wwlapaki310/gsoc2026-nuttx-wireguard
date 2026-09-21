# In-kernel WireGuard — verification matrix

Honest status of each test in [in-kernel-plan.md](in-kernel-plan.md) §3 for the **in-kernel
version**. "A script exists" and "the test passed" are tracked separately; unimplemented tests
are listed as such, not omitted.

- Target under test: fork `net-wireguard` HEAD **`677c8f54fe`** (nuttx) + fork `system-wg`
  HEAD **`24b3f311`** (nuttx-apps), base upstream/master `c95c546c`.
- Runner: the `wgdev` container (`nuttx-wireguard:sim-master`) + a local RISC-V toolchain and
  `qemu-system-riscv64` for the kernel-build runtime. See [reproduce.md](reproduce.md).
- Legend: **PASS** = run and passed; **BUILD-ONLY** = compiles/links, not run; **NOT RUN** =
  not executed against the kernel version; **NOT IMPLEMENTED** = no test written yet.
- Last updated: 2026-09-21.

## Summary

| Status | Tests |
|---|---|
| PASS (run) | T0 (partial), T1, **TF**, TR (partial), **TN**, **T3**, T6 runtime (via rv-virt), T8 (partial) |
| BUILD-ONLY | qemu-armv7a:knsh (BUILD_KERNEL build) |
| NOT RUN against kernel version | T4 runtime, T5 (hardware) |
| NOT IMPLEMENTED | TV (KAT), TZ, TE, TT, T7 |

The kernel driver's **protocol/crypto correctness** is covered by T1 + TR against a real Linux
kernel WireGuard peer, and its **kernel/user separation at runtime** by T6 (a full tunnel in a
real kernel build). The gaps below are the honest remainder for a merge-ready submission.

## Matrix

| ID | Checks | Impl / env | Status | Evidence | Pending reason |
|---|---|---|---|---|---|
| **T0** | style, SPDX headers, no key in `.config` | checkpatch/nxstyle | **PASS (partial)** | `kdev.sh style`: own files clean; `wg_x25519.c` (third-party MIT) excluded | full nxstyle sweep + `.config`-has-no-key assertion not scripted as a gate |
| **T1** | runtime-config tunnel to Linux kernel WG; handshake/ping; down/up; `saveconf`/`setconf` round-trip; on-device genkey; pubkey == wg(8) | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-runtime.sh` (2026-09-20, all PASS) | `wg show` snapshot-diff against a pinned `expected/wg-show.txt` not yet added |
| **T3** | two peers holding sessions at once | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-multipeer.sh`: two Linux WireGuard interfaces hold sessions with wg0 at once; traffic through each, both handshaken | — |
| **TF** | ioctl unit negatives (all-zero key, self-pubkey, bad endpoint, allowed-ips overlap, peer limit+1, cidr > 32, up with no key, malformed base64 key) → all rejected, `wg show` unchanged | sim | **PASS** | `scripts/kernel/verify-sim-wg-ioctl.sh` (all cases PASS) | low-order-point and undersized-buffer cases not separately exercised |
| **TV** | KAT: X25519, ChaCha20-Poly1305, XChaCha20-Poly1305, BLAKE2s, HKDF intermediates, full handshake | sim C test | **NOT IMPLEMENTED** | — | no KAT added. Directly relevant to the chachapoly nonce fix — a u64-counter ChaCha20-Poly1305 vector belongs in `crypto/testmngr.c` (see [chachapoly-nonce-draft.md](chachapoly-nonce-draft.md)) |
| **TR** | replay/spoof: (a) resent keepalive from a forged source does not move endpoint; (b) resent initiation → one reply; (c) out-of-window counter; (d) type/length fuzzing | sim, tap | **PASS (partial: a, b)** | `scripts/kernel/verify-sim-wg-replay.sh` (2026-09-20, all PASS): replay does not move endpoint; initiation flood past the load threshold draws cookie replies; tunnel survives | (c) out-of-window counter and (d) fuzzing not separately exercised |
| **TN** | negative interop: wrong peer pubkey, PSK mismatch → does not connect (with a correct-key positive control) | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-negotiation.sh` (all PASS) | the "peer under cookie load" case is covered by TR's flood instead |
| **T4** | QEMU ARM Cortex-A7 runtime (T1 equivalent) | qemu-armv7a | **NOT RUN (runtime)** | — | virtio-net was not wired on qemu-armv7a in this environment; the equivalent runtime proof was done on rv-virt instead (see T6) |
| **T5** | real hardware (ESP32-S3, Spresense) over real Wi-Fi | HIL | **NOT RUN** | — | **the kernel driver has not been flashed to hardware**; the boards currently run the apps v0.1.1 image, and re-verification is blocked while the flat's Wi-Fi is down |
| **T6** | BUILD_KERNEL: `set private-key`→`set peer`→`up`→ping 3/3 → `wg show` handshake; **TZ** MPU exception reading `priv->wg` from the `wg` task | kernel build + virtio-net | **PASS (tunnel); build also on knsh** | `scripts/kernel/verify-knetnsh-wg.sh` on **rv-virt:knetnsh64** (2026-09-20): bidirectional tunnel + ping to Linux kernel WG, `wg` loaded as a separate ELF across the syscall boundary. `scripts/kernel/build-knsh.sh`: qemu-armv7a:knsh **BUILD_KERNEL build** passes | planned vehicle was qemu-armv7a:knetnsh; rv-virt:knetnsh64 used because its virtio-net is known-good. The **TZ MPU-exception** sub-check was not run |
| **TZ** | zeroization: after `wg down`, `gcore` finds 0 copies of the known private key / session keys | sim | **NOT IMPLEMENTED** | — | not run |
| **TE** | entropy: 3 cold boots → `wg genkey` all differ; no pool `cryptwarn`; `.config` meets the RNG dependency | HIL + static | **NOT IMPLEMENTED** | — | `genkey` works and the Kconfig dependency (`CRYPTO_RANDOM_POOL`/`DEV_URANDOM_ARCH`) is enforced, but the 3-cold-boot differ test was not run |
| **TT** | time/TAI64N: re-handshake within 30 s after reboot in four scenarios | HIL | **NOT IMPLEMENTED** | — | `wg_tai64n` uses the monotonic clock (does not go backwards), but the reboot re-handshake scenarios were not run |
| **T7** | soak: 50+ rekeys across REJECT_AFTER_TIME×3, 10 endpoint changes, 100 down/up; no monotonic iob/stack growth | HIL | **NOT IMPLEMENTED** | — | not run |
| **T8** | build matrix (`sim:wireguard` on the CI hosts + a testbuild subset + CMake) | build | **PASS (partial)** | `sim:wireguard` defconfig builds; `qemu-armv7a:knsh` and `rv-virt:knetnsh64` build under BUILD_KERNEL | full `testbuild.sh` subset and the CMake path not run here; upstream CI not yet exercised |

## Bugs found and fixed during verification

| Bug | Where | Found by | Fix |
|---|---|---|---|
| ChaCha20-Poly1305 u64-nonce counter in the wrong bytes (handshake OK, data ≥ packet 2 fails) | **pre-existing NuttX** `crypto/chachapoly.c` | T1 (sim, data beyond the first packet) | `memcpy(le_nonce_array + 4, ...)`; goes upstream as a separate `crypto:` PR |
| 6 KB `wg_peer_s` snapshot on the 3 KB kernel stack → heap corruption/panic | **new driver** `wg_set_if()` | T6 (BUILD_KERNEL only; sim's larger stacks hid it) | move the snapshot to `kmm_malloc`/`kmm_free` |

## Honest remainder before a merge-ready submission

- **Write and run** TV (KAT — including the u64-counter ChaCha20-Poly1305 vector that the
  nonce fix needs).
- **Run** TZ (zeroization), TE (entropy), TT (TAI64N/reboot), T7 (soak) against the kernel
  version.
- **Hardware (T5)** for the kernel driver once Wi-Fi is back.
- TF, TN, and T3 are now covered by sim scripts (above). The remainder does not change what has
  been shown (T1 + TF + TR + TN + T3 + T6), but the items above are required by the plan's own
  §3.4 cadence before PR-K1.
