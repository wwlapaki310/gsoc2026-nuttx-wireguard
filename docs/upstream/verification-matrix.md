# In-kernel WireGuard — verification matrix

**Timestamp follow-up — now committed (`7936d68402`, 2026-09-27):** the
realtime/same-boot high-water correction has a [separate record](tai64n-design.md).
RTC-enabled sim reboot, T1 and TR passed, and the host allocator unit test
(encoding, repeat/rollback/carry, forward jump, failure atomicity, 4,000
concurrent allocations, UBSan) was re-run against the committed source.
Earlier KERNEL and hardware results predate this change and are not a rerun of
it. **#14 remains open:** boards without a trustworthy retained clock still
cannot reconnect, and durable range reservation is designed but not implemented.

**2026-09-22 concurrency follow-up:** the queued-output revision (now
`66b7403c8a`) has a separate [validation record](locking-followup.md), including lifecycle
and fault tests. **2026-09-23:** that revision was **re-verified on the real kernel build**
— `rv-virt:knetnsh64` (BUILD_KERNEL + virtio-net) tunnels bidirectionally to a Linux kernel
WireGuard peer (T6 rerun, PASS), so the redesign is not sim-only. **PROTECTED now builds**
(2026-09-27, `mr-canhubk3:knsh`): kernel and `nuttx_user.elf` both link with the driver and
`apps/system/wg`, so the pointer-free ioctl ABI holds under the MPU split as well — but this is
a build, not a run; PROTECTED *runtime* is still outstanding, as are SMP, stack measurement,
and sustained-flood availability. **Real hardware (T5) PASS on both boards (2026-09-26): the
in-kernel driver tunnels over real Wi-Fi on the ESP32-S3 (native Wi-Fi) and on the Spresense
(GS2200M over `usrsock`) to the Windows official WireGuard client. **2026-09-27 the usrsock
fault path was then forced on that board** (`DEBUG_TX_STALL`): with a datagram held outstanding
on the real backend, a query and a peer update both completed, `wg down` reported ETIMEDOUT
instead of hanging, the retained ciphertext was not mutated, and a repeated down reaped and
`wg up` recovered — the queued-output design's central claim, checked against the real
backend rather than argued from code. IOB exhaustion, Wi-Fi loss mid-send and sustained load
are still untested.**
The [2026-09-27 evidence review](hardware-followup-2026-09-27.md) separates reported
hardware observations, inspected build artifacts, and remaining fault tests. The retained
Spresense source uses the older uptime timestamp, not the uncommitted realtime correction.

Honest status of each test in [in-kernel-plan.md](in-kernel-plan.md) §3 for the **in-kernel
version**. "A script exists" and "the test passed" are tracked separately; unimplemented tests
are listed as such, not omitted.

- Target under test: fork `net-wireguard` HEAD **`7936d68402`** (nuttx) + fork `system-wg`
  HEAD **`3272db33`** (nuttx-apps), base upstream/master `c95c546c`. **Both fork trees are now
  clean — the TAI64N realtime correction that was previously uncommitted is in `7936d68402`.**
  The sim and rv-virt runs below, and the 2026-09-26 hardware runs, were made on the earlier
  `66b7403c8a` / `24b3f311`; the newer commits (`IOB_NCHAINS` guard + AEAD KATs, the
  configuration-save fix, the TAI64N correction) are covered by builds, the TAI64N host unit
  test, T1 and the key-save fault test — **not** by a hardware rerun. The 2026-09-27 Spresense
  headless image was built from `nuttx-13.0.1` with the driver patch taken at `7936d68402`.
- Runner: the `wgdev` container (`nuttx-wireguard:sim-master`) + a local RISC-V toolchain and
  `qemu-system-riscv64` for the kernel-build runtime. See [reproduce.md](reproduce.md).
- Legend: **PASS** = run and passed; **BUILD-ONLY** = compiles/links, not run; **NOT RUN** =
  not executed against the kernel version; **NOT IMPLEMENTED** = no test written yet.
- Last updated: 2026-09-28.

## Summary

| Status | Tests |
|---|---|
| PASS (run) | **T0**, T1, **TF**, **TV** (chachapoly + xchacha + X25519 + BLAKE2s KAT), TR (partial), **TN**, **T3**, T6 runtime (via rv-virt), **T5 (ESP32-S3 + Spresense/usrsock hardware)**, **TZ**, **TE (Spresense)**, **T7 (sim)**, T8 (partial) |
| BUILD-ONLY | qemu-armv7a:knsh (BUILD_KERNEL build) |
| NOT RUN against kernel version | T4 runtime (T5 hardware PASS on both boards) |
| PARTIAL / unresolved | TT (RTC-enabled sim reboot passes; persistence/reboot rollback remain open) |
| NOT IMPLEMENTED | TV extras (HKDF-intermediate/full-handshake KATs) |

The kernel driver's **protocol/crypto correctness** is covered by T1 + TR against a real Linux
kernel WireGuard peer, and its **kernel/user separation at runtime** by T6 (a full tunnel in a
real kernel build). The gaps below are the honest remainder for a merge-ready submission.

## Matrix

| ID | Checks | Impl / env | Status | Evidence | Pending reason |
|---|---|---|---|---|---|
| **T0** | style, SPDX headers, no key in `.config` | checkpatch/nxstyle | **PASS** | `kdev.sh style` (2026-09-28): `checkpatch.sh -f` over every driver `.c`/`.h`, the public header, and **every** `apps/system/wg` `.c`/`.h` — "All checks pass" on both trees, plus an assertion that nothing base64-key-shaped appears in `.config` ("OK: no key material in .config"). The vendored `wg_x25519.c`/`.h` are excluded the way NuttX already excludes vendor sources: path entries in `g_white_files[]` in `tools/nxstyle.c`, next to the PHY62XX/Infineon/GD32VW55x entries | The exclusion adds one apps path to the nuttx repo, so **PR-A1's style depends on PR-K1 landing first**. Not yet run as upstream CI does it (`checkpatch.sh -g <range>` over the actual commits); the `-f` sweep is over the files, which is stricter per file but does not check the patch form (rename/whitespace-in-diff). Adaptation cruft in the vendored files was cleaned up at the same time: commented-out `strobe.h` includes, a missing `sys/endian.h` that left `BYTE_ORDER` undefined so the big-endian `#error` could never fire, and one trailing-whitespace line |
| **T1** | runtime-config tunnel to Linux kernel WG; handshake/ping; down/up; `saveconf`/`setconf` round-trip; on-device genkey; pubkey == wg(8) | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-runtime.sh` (2026-09-20, all PASS) | `wg show` snapshot-diff against a pinned `expected/wg-show.txt` not yet added |
| **T3** | two peers holding sessions at once | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-multipeer.sh`: two Linux WireGuard interfaces hold sessions with wg0 at once; traffic through each, both handshaken | — |
| **TF** | ioctl unit negatives (all-zero key, self-pubkey, bad endpoint, allowed-ips overlap, peer limit+1, cidr > 32, up with no key, malformed base64 key) → all rejected, `wg show` unchanged | sim | **PASS** | `scripts/kernel/verify-sim-wg-ioctl.sh` (all cases PASS) | low-order-point and undersized-buffer cases not separately exercised |
| **TV** | KAT for the u64-counter ChaCha20-Poly1305 (the nonce fix), **XChaCha20-Poly1305 (cookie path, incl. HChaCha20), X25519 and BLAKE2s-256**; HKDF-intermediate/full-handshake KATs still open | C test vs NuttX `crypto/` | **PASS (chachapoly + xchacha + X25519 + BLAKE2s)** | `scripts/kernel/chachapoly_kat.c` + `crypto_kat.c` + `verify-sim-wg-kat.sh`: chachapoly counters 0/1/2 match pyca/cryptography, decrypt round-trips, forged tags rejected; **xchacha20poly1305** matches libsodium (draft-irtf-cfrg-xchacha prefix); **X25519** matches RFC 7748 §5.2 and §6.1 (keygen); **BLAKE2s-256** matches the reference `""`/`"abc"` digests — all run against `crypto/chachapoly.c`, `crypto/curve25519.c`, `crypto/blake2s.c` (2026-09-23) | catches the bytes-0..7 nonce bug (counters ≥ 1 differ); the xchacha vector confirms the same fix repairs the cookie path (A6). HKDF-intermediate and full-handshake KATs not yet added. The chachapoly + xchacha vectors are **also in `crypto/testmngr.c`/`testmngr.h`** (run at boot under `CONFIG_CRYPTO_ALGTEST` — verified: `up_cryptoinitialize: crypto test OK`), ready to fold into the `crypto:` PR commit |
| **TR** | replay/spoof: (a) resent keepalive from a forged source does not move endpoint; (b) resent initiation → one reply; (c) out-of-window counter; (d) type/length fuzzing | sim, tap | **PASS (partial: a, b)** | `scripts/kernel/verify-sim-wg-replay.sh` (2026-09-20, all PASS): replay does not move endpoint; initiation flood past the load threshold draws cookie replies; tunnel survives | (c) out-of-window counter and (d) fuzzing not separately exercised |
| **TN** | negative interop: wrong peer pubkey, PSK mismatch → does not connect (with a correct-key positive control) | sim, tap | **PASS** | `scripts/kernel/verify-sim-wg-negotiation.sh` (all PASS) | the "peer under cookie load" case is covered by TR's flood instead |
| **T4** | QEMU ARM Cortex-A7 runtime (T1 equivalent) | qemu-armv7a | **NOT RUN (runtime)** | — | virtio-net was not wired on qemu-armv7a in this environment; the equivalent runtime proof was done on rv-virt instead (see T6) |
| **T5** | real hardware (ESP32-S3, Spresense) over real Wi-Fi | HIL | **PASS (reported functional connectivity, both boards)** | 2026-09-26 report: **ESP32-S3**, native Wi-Fi, Windows tunnel ping 4/4; **Spresense**, GS2200M/usrsock, recent handshake, TX/RX 496 B each, Windows tunnel ping 6/6 (~5-8 ms), SmartFS configuration restored with Windows peer unchanged. [Artifact review and scope](hardware-followup-2026-09-27.md): retained Spresense image hash matches container output, base tag is `nuttx-13.0.1`, queued-output source is present. Not an independent hardware rerun. **Redacted Spresense serial + ping transcript now tracked:** [evidence/spresense-kernel-2026-09-26.md](evidence/spresense-kernel-2026-09-26.md) (ESP32-S3 serial was not captured) | FLAT hardware runs do not demonstrate BUILD_KERNEL/PROTECTED isolation. Usrsock blocked-send/control/stop faults, sustained load, and handshake direction on reboot remain untested by this report. Retained source includes RTC/clock/GS2200M changes and the older uptime TAI64N. **Early-boot cause isolated 2026-09-27 (supersedes the earlier "build-cache quirk" wording): it is the known cxd56 `CONFIG_RTC_HIRES` regression ([#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)).** A fresh `nuttx-13.0.1` clone plus only the `clock_systime_timespec.c` RTC fallback boots and tunnels (ping 6/6); the earlier fresh tree lacked it because the patch step had silently failed to match. The `cxd56_rtc.c` recursive-lock patch alone did **not** fix it. A cache-free reproducible build now exists (`scripts/kernel/build-spresense-kernel.sh`, [recipe](evidence/spresense-kernel-2026-09-26.md#cache-free-reproducible-build)), and **headless bring-up is verified: the board was reset with no serial input and answered `ping 10.11.0.2` 6/6 32 s later** ([record](evidence/spresense-kernel-2026-09-26.md#headless-bring-up-2026-09-27)). Retry/diagnostics on AP or DHCP failure are not covered |
| **T6** | BUILD_KERNEL: `set private-key`→`set peer`→`up`→ping 3/3 → `wg show` handshake; **TZ** MPU exception reading `priv->wg` from the `wg` task | kernel build + virtio-net | **PASS (tunnel); build also on knsh** | `scripts/kernel/verify-knetnsh-wg.sh` on **rv-virt:knetnsh64** (2026-09-20): bidirectional tunnel + ping to Linux kernel WG, `wg` loaded as a separate ELF across the syscall boundary. `scripts/kernel/build-knsh.sh`: qemu-armv7a:knsh **BUILD_KERNEL build** passes | planned vehicle was qemu-armv7a:knetnsh; rv-virt:knetnsh64 used because its virtio-net is known-good. The **TZ MPU-exception** sub-check was not run |
| **TZ** | zeroization: what survives `wg down` in live process memory | sim | **PASS** | `scripts/kernel/verify-sim-wg-zeroize.sh` (2026-09-28). Reads the running sim's writable memory, locates `wg_device_s` by matching the known static key *and* its public key, finds the peer by public key, and checks the secret fields **by name** at offsets computed from the driver's own headers at run time. A rekey is forced first so `prev_keypair` really holds a key before the measurement. Result: `curr`/`prev` `sending_key` and `receiving_key` all 32/32 bytes set while up → **all zero after down**; re-up derives fresh keys; the static key stays resident (by design) and the whole writable address space holds **exactly one** raw copy of it, before and after. Also observed: `handshake.ephemeral_private`/`chaining_key`/`hash` are already zero *while up* — `wg_start_session()` wipes the handshake and its stack copy of the keypair as soon as the keys are derived | The plan's original wording ("0 copies of the private key") was **wrong for this design** and was changed: the static key is deliberately kept so `wg up` works again without reconfiguring, and the config file holds it anyway. `next_keypair` and the handshake fields were never non-zero at measurement time, so their clearing is **asserted by code reading only**, not exercised. Sim heap/stack behaviour is not identical to a target allocator |
| **TE** | entropy: restarts → `wg genkey` all differ; `.config` meets the RNG dependency | HIL + static | **PASS (Spresense)** | `scripts/kernel/verify-spresense-entropy.py` (2026-09-28), 3 restarts of the in-kernel Spresense image: SHA-256 prefixes `b7a636c062ee38ca`, `951aac062383a206`, `c01587b41aa089c7` — 3 distinct keys, PASS (keys themselves never printed). Config confirms the pool path rather than the constant-seed path: `CONFIG_DEV_URANDOM_RANDOM_POOL=y`, `CONFIG_CRYPTO_RANDOM_POOL=y`, `CONFIG_CRYPTO_RANDOM_POOL_COLLECT_IRQ_RANDOMNESS=y`, `# CONFIG_DEV_URANDOM_XORSHIFT128 is not set` | **DTR resets, not power cycles.** A compile-time constant seed would show identically either way (that is the failure mode being tested), but a seed that survives reset and is lost on power-off would not be distinguished. Three distinct keys detect the catastrophic case; they are not a measure of entropy quality — no statistical testing, and no check that the IRQ pool had actually been stirred by boot. Not repeated on ESP32-S3 |
| **TT** | time/TAI64N: re-handshake within 30 s after reboot in four scenarios | sim + host C + HIL | **PARTIAL (RTC sim PASS)** | [Baseline failure](tai64n-reboot.md); [partial correction](tai64n-design.md), 2026-09-23: same-key reboot with retained realtime, response at 0.695 s; actual allocator tested for rollback and 4,000 concurrent allocations | #14 remains a pre-merge blocker: high-water state is RAM-only; RTC-less/persistence/cross-reboot rollback and HIL are not completed |
| **T7** | soak: 50+ rekeys across REJECT_AFTER_TIME×3, 10 endpoint changes, 100 down/up; no monotonic iob/stack growth | sim (planned: HIL) | **PASS (sim)** | `scripts/kernel/verify-sim-wg-soak.sh` (2026-09-28), ~15 min: **151 forced rekeys over 620 s** (> `REJECT_AFTER_TIME`×3 = 540 s, so sessions expire and are rebuilt), **0 failures**; **10 endpoint changes** (both ends move port, interface cycled so NuttX must dial the new endpoint, traffic confirmed each time); **100 down/up cycles**. Resources sampled throughout via `free`, `/proc/iobinfo` and `ps` (`CONFIG_STACK_COLORATION`): heap in use and the **live allocation count return exactly to the boot values** (116 → 118 while up → 116 at rest), the **IOB pool is 24/24 in every sample** with `nwait` 0 throughout, and the `wg_rx` stack high-water is **2536 B and does not grow** after the first sample — against a `RXSTACKSIZE` default of 6144. Full table: [evidence/sim-soak-2026-09-28.md](evidence/sim-soak-2026-09-28.md) | **Sim, not hardware** — the allocator is the host's and 64-bit frames are larger than a target's, so the stack figure is conservative but the heap figures do not transfer; `ps` `FILLED` % is meaningless because the sim gives the thread ~72 KB, not 6144. ~15 min, not an endurance run. Rekeys are forced by the peer, so WireGuard's own rekey *timers* are exercised far less than the responder path. The pool never went below full, so IOB throttling/`nwait` behaviour is untested, as are the Wi-Fi/usrsock threads' stack high-water on a board |
| **T8** | build matrix (`sim:wireguard` on the CI hosts + a testbuild subset + CMake) | build | **PASS (partial)** | `sim:wireguard` defconfig builds; `qemu-armv7a:knsh` and `rv-virt:knetnsh64` build under BUILD_KERNEL; **the kernel driver also builds for `esp32s3-devkit:wifi` (Xtensa, 784 KB) and `spresense:wifi` (Cortex-M4F, 443 KB)** with kernel `NET_WIREGUARD`+`SYSTEM_WG`+`CRYPTO`; **`mr-canhubk3:knsh` links under `BUILD_PROTECTED`** — kernel 170 KB of 1 MB `kflash`, 30 KB of 128 KB `ksram`, plus a 147 KB `nuttx_user.elf` carrying `wg` | full `testbuild.sh` subset and the CMake path not run here; upstream CI not yet exercised. PROTECTED is a build only, not a run. **Size floor observed:** `lm3s6965-ek:qemu-protected` (128 KB `kflash`, 20 KB `ksram`) overflows at 115%/112% with the driver and NuttX `crypto/` in the kernel image, although its `nuttx_user.elf` still links — so boards of that class are out of reach without trimming |

## Bugs found and fixed during verification

| Bug | Where | Found by | Fix |
|---|---|---|---|
| ChaCha20-Poly1305 u64-nonce counter in the wrong bytes (handshake OK, data ≥ packet 2 fails) | **pre-existing NuttX** `crypto/chachapoly.c` | T1 (sim, data beyond the first packet) | `memcpy(le_nonce_array + 4, ...)`; goes upstream as a separate `crypto:` PR |
| 6 KB `wg_peer_s` snapshot on the 3 KB kernel stack → heap corruption/panic | **new driver** `wg_set_if()` | T6 (BUILD_KERNEL only; sim's larger stacks hid it) | move the snapshot to `kmm_malloc`/`kmm_free` |
| Build fails (`field 'rxqueue' has incomplete type`) when `CONFIG_IOB_NCHAINS == 0`; the driver's `netpkt_queue_t rxqueue` is `struct iob_queue_s`, defined only for `IOB_NCHAINS > 0` | **new driver** (config) | T5 (Spresense: `spresense:wifi` defaults `IOB_NCHAINS=0`; esp32s3:wifi had it > 0 so this was hidden) | `mm/iob/Kconfig`: `default IOB_NBUFFERS if NET_WIREGUARD` so any config enabling the driver gets a working default (verified: clean eval → 8, builds), plus a `#error` in `wireguard.c` so a configuration that still pins `IOB_NCHAINS=0` is rejected by name instead of on an incomplete type |
| `wg set private-key` could report success when the key was not persisted (`wg_record_private_key` returned void, ignoring `fopen`/`ferror`/`fclose`/`rename`); both it and `saveconf` also `unlink()`-ed the config before `rename()`, so a failed replacement or power loss destroyed the previous file | **new app** `apps/system/wg` | code review (Codex, #17), confirmed against the source | errors propagated and surfaced (`set private-key` exits nonzero and warns the running key is unsaved); new `wg_replace_file()` tries `rename()` first and only falls back to unlink-then-rename if the filesystem refuses, reporting where the content was left. Build + T1 regression PASS, plus `scripts/kernel/verify-sim-wg-keyfile-faults.sh` (injects an unopenable temporary path; asserts nonzero exit, the "not stored" diagnostic, that the previous configuration survives, and recovery — 5/5 PASS). **Still open:** ENOSPC/close-failure injection, two writers, and SmartFS rename/power-cut durability ([review](keyfile-correctness-review.md)) |
| Blocking send drops the lock mid-transmit → shared `cryptbuf` / live keypair could be corrupted | **new driver** send path | design review (Codex, #12) | **final design (queued output):** protocol state under the device `d_lock`; datagrams encrypted into an immutable bounded queue; only the RX thread sends, outside `d_lock` and without live-state refs. Backend-independent. See [locking-followup.md](locking-followup.md) |
| `wg_ifdown` could close the socket from under a still-running RX thread; a timed-out stop had no recovery | **new driver** `wg_ifdown` | design review (Codex, #12) | RX loop re-checks `running`; the stop releases `d_lock` while waiting; `reaping` excludes a second waiter; a repeated ifdown reaps a stopping interface |

Lifecycle tests added for these: `verify-sim-wg-downup.sh` (down/up under an inbound flood, per-command assertions) and `verify-sim-wg-stop-recovery.sh` (deliberate stop timeout + recovery, needs the debug Kconfig). Both PASS; regression (T1/TF/TR/TN/T3) unaffected. **Scope:** sim covers buffered UDP and injected worker stalls. The queued-output design supersedes the earlier `sending`-guard argument. T5 now adds normal usrsock hardware connectivity, but does not force the actual blocked-send/control/stop interleavings; those remain targeted tests to run.

## Honest remainder before a merge-ready submission

- **Extend TV**: the ChaCha20-Poly1305, XChaCha20-Poly1305 (incl. HChaCha20), X25519 (RFC 7748)
  and BLAKE2s-256 KATs are done and run; still to add are HKDF-intermediate and full-handshake
  KATs, and to land the chachapoly + xchacha vectors in `crypto/testmngr.c` with the `crypto:` PR.
- **Run** TT (TAI64N/reboot) against the kernel version, and repeat T7 on hardware. TZ, TE and
  T7 are done (2026-09-28); their residual gaps are `next_keypair`/handshake clearing not being
  exercised at measurement time, TE being reset-based rather than power-cycle-based, and T7 being
  a 15-minute sim run whose heap figures do not transfer to a board.
- **Hardware (T5)**: **done on both boards (2026-09-26)** — ESP32-S3 (native Wi-Fi) and Spresense
  (GS2200M/usrsock). Follow-ups: a proper Dockerfile stage for the kernel Spresense image (built
  on 13.0.1), headless `rcS` auto-config, and BUILD_KERNEL/PROTECTED on the boards.
- TF, TV (chachapoly), TN, and T3 are now covered by sim scripts/tests. The remainder does not
  change what has been shown (T1 + TF + TV + TR + TN + T3 + T6), but the items above are required
  by the plan's own §3.4 cadence before PR-K1.
