# In-kernel WireGuard — verification matrix

**2026-09-23 timestamp follow-up (uncommitted):** the realtime/same-boot
high-water correction has a [separate record](tai64n-design.md). RTC-enabled
sim reboot, T1, and TR passed on this working tree. Earlier KERNEL results
below do not constitute a rerun of this timestamp change; #14 remains open.

**2026-09-22 concurrency follow-up:** the queued-output revision (now
`66b7403c8a`) has a separate [validation record](locking-followup.md), including lifecycle
and fault tests. **2026-09-23:** that revision was **re-verified on the real kernel build**
— `rv-virt:knetnsh64` (BUILD_KERNEL + virtio-net) tunnels bidirectionally to a Linux kernel
WireGuard peer (T6 rerun, PASS), so the redesign is not sim-only. **PROTECTED** (a distinct
MPU build, not exercised anywhere yet) is still outstanding, as are SMP, stack measurement,
and sustained-flood availability. **Real hardware (T5) PASS on both boards (2026-09-26): the
in-kernel driver tunnels over real Wi-Fi on the ESP32-S3 (native Wi-Fi) and on the Spresense
(GS2200M over `usrsock`) to the Windows official WireGuard client — functional usrsock
interoperability on hardware, not a proof of all blocking/concurrency failure modes.**
The [2026-09-27 evidence review](hardware-followup-2026-09-27.md) separates reported
hardware observations, inspected build artifacts, and remaining fault tests. The retained
Spresense source uses the older uptime timestamp, not the uncommitted realtime correction.

Honest status of each test in [in-kernel-plan.md](in-kernel-plan.md) §3 for the **in-kernel
version**. "A script exists" and "the test passed" are tracked separately; unimplemented tests
are listed as such, not omitted.

- Target under test: fork `net-wireguard` HEAD **`66b7403c8a`** (nuttx) + fork `system-wg`
  HEAD **`24b3f311`** (nuttx-apps), base upstream/master `c95c546c`.
- Runner: the `wgdev` container (`nuttx-wireguard:sim-master`) + a local RISC-V toolchain and
  `qemu-system-riscv64` for the kernel-build runtime. See [reproduce.md](reproduce.md).
- Legend: **PASS** = run and passed; **BUILD-ONLY** = compiles/links, not run; **NOT RUN** =
  not executed against the kernel version; **NOT IMPLEMENTED** = no test written yet.
- Last updated: 2026-09-27.

## Summary

| Status | Tests |
|---|---|
| PASS (run) | T0 (partial), T1, **TF**, **TV** (chachapoly + xchacha + X25519 + BLAKE2s KAT), TR (partial), **TN**, **T3**, T6 runtime (via rv-virt), **T5 (ESP32-S3 + Spresense/usrsock hardware)**, T8 (partial) |
| BUILD-ONLY | qemu-armv7a:knsh (BUILD_KERNEL build) |
| NOT RUN against kernel version | T4 runtime (T5 hardware PASS on both boards) |
| PARTIAL / unresolved | TT (RTC-enabled sim reboot passes; persistence/reboot rollback remain open) |
| NOT IMPLEMENTED | TV extras (HKDF-intermediate/full-handshake KATs), TZ, TE, T7 |

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
| **TV** | KAT for the u64-counter ChaCha20-Poly1305 (the nonce fix), **XChaCha20-Poly1305 (cookie path, incl. HChaCha20), X25519 and BLAKE2s-256**; HKDF-intermediate/full-handshake KATs still open | C test vs NuttX `crypto/` | **PASS (chachapoly + xchacha + X25519 + BLAKE2s)** | `scripts/kernel/chachapoly_kat.c` + `crypto_kat.c` + `verify-sim-wg-kat.sh`: chachapoly counters 0/1/2 match pyca/cryptography, decrypt round-trips, forged tags rejected; **xchacha20poly1305** matches libsodium (draft-irtf-cfrg-xchacha prefix); **X25519** matches RFC 7748 §5.2 and §6.1 (keygen); **BLAKE2s-256** matches the reference `""`/`"abc"` digests — all run against `crypto/chachapoly.c`, `crypto/curve25519.c`, `crypto/blake2s.c` (2026-09-23) | catches the bytes-0..7 nonce bug (counters ≥ 1 differ); the xchacha vector confirms the same fix repairs the cookie path (A6). HKDF-intermediate and full-handshake KATs not yet added. The chachapoly + xchacha vectors are **also in `crypto/testmngr.c`/`testmngr.h`** (run at boot under `CONFIG_CRYPTO_ALGTEST` — verified: `up_cryptoinitialize: crypto test OK`), ready to fold into the `crypto:` PR commit |
| **TR** | replay/spoof: (a) resent keepalive from a forged source does not move endpoint; (b) resent initiation → one reply; (c) out-of-window counter; (d) type/length fuzzing | sim, tap | **PASS (partial: a, b)** | `scripts/kernel/verify-sim-wg-replay.sh` (2026-09-20, all PASS): replay does not move endpoint; initiation flood past the load threshold draws cookie replies; tunnel survives | (c) out-of-window counter and (d) fuzzing not separately exercised |
| **TN** | negative interop: wrong peer pubkey, PSK mismatch → does not connect (with a correct-key positive control) | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-negotiation.sh` (all PASS) | the "peer under cookie load" case is covered by TR's flood instead |
| **T4** | QEMU ARM Cortex-A7 runtime (T1 equivalent) | qemu-armv7a | **NOT RUN (runtime)** | — | virtio-net was not wired on qemu-armv7a in this environment; the equivalent runtime proof was done on rv-virt instead (see T6) |
| **T5** | real hardware (ESP32-S3, Spresense) over real Wi-Fi | HIL | **PASS (reported functional connectivity, both boards)** | 2026-09-26 report: **ESP32-S3**, native Wi-Fi, Windows tunnel ping 4/4; **Spresense**, GS2200M/usrsock, recent handshake, TX/RX 496 B each, Windows tunnel ping 6/6 (~5-8 ms), SmartFS configuration restored with Windows peer unchanged. [Artifact review and scope](hardware-followup-2026-09-27.md): retained Spresense image hash matches container output, base tag is `nuttx-13.0.1`, queued-output source is present. Not an independent hardware rerun | FLAT hardware runs do not demonstrate BUILD_KERNEL/PROTECTED isolation. Usrsock blocked-send/control/stop faults, sustained load, and handshake direction on reboot remain untested by this report. Retained source includes RTC/clock/GS2200M changes and the older uptime TAI64N. Fresh-tree boot failure also occurs in a plain control according to the report; exact cause is not isolated. Reproducible build manifest and headless kernel `rcS` remain follow-ups |
| **T6** | BUILD_KERNEL: `set private-key`→`set peer`→`up`→ping 3/3 → `wg show` handshake; **TZ** MPU exception reading `priv->wg` from the `wg` task | kernel build + virtio-net | **PASS (tunnel); build also on knsh** | `scripts/kernel/verify-knetnsh-wg.sh` on **rv-virt:knetnsh64** (2026-09-20): bidirectional tunnel + ping to Linux kernel WG, `wg` loaded as a separate ELF across the syscall boundary. `scripts/kernel/build-knsh.sh`: qemu-armv7a:knsh **BUILD_KERNEL build** passes | planned vehicle was qemu-armv7a:knetnsh; rv-virt:knetnsh64 used because its virtio-net is known-good. The **TZ MPU-exception** sub-check was not run |
| **TZ** | zeroization: after `wg down`, `gcore` finds 0 copies of the known private key / session keys | sim | **NOT IMPLEMENTED** | — | not run |
| **TE** | entropy: 3 cold boots → `wg genkey` all differ; no pool `cryptwarn`; `.config` meets the RNG dependency | HIL + static | **NOT IMPLEMENTED** | — | `genkey` works and the Kconfig dependency (`CRYPTO_RANDOM_POOL`/`DEV_URANDOM_ARCH`) is enforced, but the 3-cold-boot differ test was not run |
| **TT** | time/TAI64N: re-handshake within 30 s after reboot in four scenarios | sim + host C + HIL | **PARTIAL (RTC sim PASS)** | [Baseline failure](tai64n-reboot.md); [partial correction](tai64n-design.md), 2026-09-23: same-key reboot with retained realtime, response at 0.695 s; actual allocator tested for rollback and 4,000 concurrent allocations | #14 remains a pre-merge blocker: high-water state is RAM-only; RTC-less/persistence/cross-reboot rollback and HIL are not completed |
| **T7** | soak: 50+ rekeys across REJECT_AFTER_TIME×3, 10 endpoint changes, 100 down/up; no monotonic iob/stack growth | HIL | **NOT IMPLEMENTED** | — | not run |
| **T8** | build matrix (`sim:wireguard` on the CI hosts + a testbuild subset + CMake) | build | **PASS (partial)** | `sim:wireguard` defconfig builds; `qemu-armv7a:knsh` and `rv-virt:knetnsh64` build under BUILD_KERNEL; **the kernel driver also builds for `esp32s3-devkit:wifi` (Xtensa, 784 KB) and `spresense:wifi` (Cortex-M4F, 443 KB)** with kernel `NET_WIREGUARD`+`SYSTEM_WG`+`CRYPTO` | full `testbuild.sh` subset and the CMake path not run here; upstream CI not yet exercised |

## Bugs found and fixed during verification

| Bug | Where | Found by | Fix |
|---|---|---|---|
| ChaCha20-Poly1305 u64-nonce counter in the wrong bytes (handshake OK, data ≥ packet 2 fails) | **pre-existing NuttX** `crypto/chachapoly.c` | T1 (sim, data beyond the first packet) | `memcpy(le_nonce_array + 4, ...)`; goes upstream as a separate `crypto:` PR |
| 6 KB `wg_peer_s` snapshot on the 3 KB kernel stack → heap corruption/panic | **new driver** `wg_set_if()` | T6 (BUILD_KERNEL only; sim's larger stacks hid it) | move the snapshot to `kmm_malloc`/`kmm_free` |
| Build fails (`field 'rxqueue' has incomplete type`) when `CONFIG_IOB_NCHAINS == 0`; the driver's `netpkt_queue_t rxqueue` is `struct iob_queue_s`, defined only for `IOB_NCHAINS > 0` | **new driver** (config) | T5 (Spresense: `spresense:wifi` defaults `IOB_NCHAINS=0`; esp32s3:wifi had it > 0 so this was hidden) | `mm/iob/Kconfig`: `default IOB_NBUFFERS if NET_WIREGUARD` so any config enabling the driver gets a working default (verified: clean eval → 8, builds) |
| Blocking send drops the lock mid-transmit → shared `cryptbuf` / live keypair could be corrupted | **new driver** send path | design review (Codex, #12) | **final design (queued output):** protocol state under the device `d_lock`; datagrams encrypted into an immutable bounded queue; only the RX thread sends, outside `d_lock` and without live-state refs. Backend-independent. See [locking-followup.md](locking-followup.md) |
| `wg_ifdown` could close the socket from under a still-running RX thread; a timed-out stop had no recovery | **new driver** `wg_ifdown` | design review (Codex, #12) | RX loop re-checks `running`; the stop releases `d_lock` while waiting; `reaping` excludes a second waiter; a repeated ifdown reaps a stopping interface |

Lifecycle tests added for these: `verify-sim-wg-downup.sh` (down/up under an inbound flood, per-command assertions) and `verify-sim-wg-stop-recovery.sh` (deliberate stop timeout + recovery, needs the debug Kconfig). Both PASS; regression (T1/TF/TR/TN/T3) unaffected. **Scope:** sim covers buffered UDP and injected worker stalls. The queued-output design supersedes the earlier `sending`-guard argument. T5 now adds normal usrsock hardware connectivity, but does not force the actual blocked-send/control/stop interleavings; those remain targeted tests to run.

## Honest remainder before a merge-ready submission

- **Extend TV**: the ChaCha20-Poly1305, XChaCha20-Poly1305 (incl. HChaCha20), X25519 (RFC 7748)
  and BLAKE2s-256 KATs are done and run; still to add are HKDF-intermediate and full-handshake
  KATs, and to land the chachapoly + xchacha vectors in `crypto/testmngr.c` with the `crypto:` PR.
- **Run** TZ (zeroization), TE (entropy), TT (TAI64N/reboot), T7 (soak) against the kernel
  version.
- **Hardware (T5)**: **done on both boards (2026-09-26)** — ESP32-S3 (native Wi-Fi) and Spresense
  (GS2200M/usrsock). Follow-ups: a proper Dockerfile stage for the kernel Spresense image (built
  on 13.0.1), headless `rcS` auto-config, and BUILD_KERNEL/PROTECTED on the boards.
- TF, TV (chachapoly), TN, and T3 are now covered by sim scripts/tests. The remainder does not
  change what has been shown (T1 + TF + TV + TR + TN + T3 + T6), but the items above are required
  by the plan's own §3.4 cadence before PR-K1.
