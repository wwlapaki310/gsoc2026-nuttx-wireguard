# Open design & discussion points (talk + upstream)

Updated 2026-09-27. Priorities, implementation tasks, and acceptance criteria:
[remaining-work.md](remaining-work.md). This page tracks design choices, not
a duplicate test-status list.

One place for the development questions that are still open — for the Community Over Code talk
(what to put to the audience) and for the upstream review. This **consolidates existing
material** (the review brief's questions, the design doc's open items, the verification matrix,
and issues #3/#5/#6/#9/#10), updated against the evidence and code review. Each item notes a *current position* where
one exists — a position, not a settled decision.

Sources: [in-kernel-review-brief.md §7](in-kernel-review-brief.md) ·
[in-kernel-design.md](in-kernel-design.md) · [verification-matrix.md](verification-matrix.md) ·
[in-kernel-plan.md](in-kernel-plan.md) §6.

## A. Design & ABI

1. **ioctl ABI shape.** Flat fixed-size ioctl vs a netlink-style interface for future growth
   (IPv6, more peers). *Position:* flat & pointer-free because PROTECTED/KERNEL copy the caller's
   struct directly. Flags can select optional fields, but the fixed `sockaddr_in`
   endpoint needs a new layout/command for IPv6. Open: is this versioning policy
   acceptable? Netlink is an alternative to discuss, not a known requirement.
2. **RX = a kthread on `psock_poll`.** vs interrupt-driven or reusing an existing lower-half RX
   path. *Position:* a kthread mirrors `rpmsgdrv.c`; open: stall/priority-inversion risk.
3. **Kernel owns the UDP socket + thread — lifecycle.** ifup creates / ifdown tears down.
   *Position:* protocol state under device `d_lock`, immutable queued output,
   RX worker sends without that lock. Open: actual usrsock blocked-send/control/
   ifdown/reap tests, beyond normal hardware connectivity (see design doc).
4. **Private key write-only + config file as source of truth.** The get ioctl never returns the
   key; it lives in the user-side config file. Open: acceptable for backup/migration, or add a
   Kconfig-gated read-back for debugging?
   Separate correctness work: [#17](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/17)
   tracks unreported key-save failures and deleting the original before replacement.
5. **Vendored `wg_x25519.c` (MIT) in the `wg` command.** For offline genkey/pubkey (one call:
   `x25519(pub, priv, base, 1)` for `wg pubkey`). *Position:* **kept.** Investigated 2026-09-23:
   the kernel already ships `crypto/curve25519.c` and the in-kernel driver calls it directly
   (`wg_crypto.c` → `curve25519()`/`curve25519_generate_public()`), but that path is *in-kernel
   only*. The `/dev/crypto` cryptodev ABI has **no** asymmetric op for X25519/Curve25519 — its
   `CRK_*` list stops at `MOD_EXP`, `DSA`, `DH`, `RSA`, `ECDSA_SECP256R1` — so under
   PROTECTED/KERNEL there is no syscall surface a userspace `wg pubkey` could use, and linking the
   kernel symbol only "works" in FLAT (defeating the migration's purpose). Vendoring a small,
   self-contained userspace X25519 also matches upstream `wireguard-tools`, which vendors its own
   `curve25519.c`. Its nxstyle non-conformance is expected third-party formatting (kept verbatim
   under MIT). Open (larger, separate work): add a `CRK_CURVE25519` cryptodev op upstream so any
   future userspace WG tooling could share the kernel implementation — not a blocker for this PR.
6. **crypto nonce fix as a separate `crypto:` PR.** Order and granularity; add a u64-counter KAT
   to `crypto/testmngr.c`. **xchacha resolved 2026-09-23:** `xchacha20poly1305_encrypt/decrypt`
   does *not* take a byte nonce end-to-end — it splits the 24-byte nonce into an HChaCha20 subkey
   (`nonce[0:16]`) and a u64 remainder (`nonce[16:24]`, read `le64toh`) that it hands to the same
   `chacha20poly1305_encrypt(..., h_nonce, subkey)`. So the u64→IV-bytes-`[4:12]` fix covers the
   cookie path too — **no separate xchacha fix is needed** — and the resulting 96-bit IV
   (`0x00000000 || nonce[16:24]`) is the standard XChaCha20-Poly1305 construction (verified by
   inspection). Before the fix xchacha was equally broken (remainder landed in IV bytes `[0:8]`);
   the shared fix repairs both. The driver *does* exercise this path (cookie replies:
   `wg_noise.c` uses `wg_xaead_encrypt`/`decrypt`), and TR draws cookie replies under an
   initiation flood. XChaCha20-Poly1305 KAT coverage was subsequently added and run;
   the local `crypto/testmngr` vectors await integration into the crypto PR commit.
   Do not keep listing this vector as unimplemented. See the verification matrix.
7. **Defaults & caps:** anti-replay window 2048, `MAX_PEERS`, `MAX_AIPS`, and the static memory
   they cost (~1.5 KB + 3 replay windows per peer). Open: right defaults/limits?
8. **Is the verification enough?** sim + rv-virt (BUILD_KERNEL) + hardware (in-kernel FLAT
   functional connectivity on both boards) — plus the
   still-missing tests (§B). Open: what extra load/negative tests would a maintainer want?
9. **Commit split:** (a) `crypto:` nonce, (b) `net/wireguard` (ABI+driver+crypto+config+docs),
   (c) `apps/system/wg`. Open: split (b) further?
10. **Safety review:** thread-safety, endianness, memory safety — the places most worth a second
    pair of eyes (the stack-overflow bug is fixed; review `d_lock` coverage, ownership,
    lock ordering, and worker lifecycle against the queued-output implementation).

## B. Verification / testing (still open)

From [verification-matrix.md](verification-matrix.md): TF, T3, TN, basic crypto KATs,
and functional T5 on both boards are now covered. TZ (zeroization), TE (cold-boot
entropy), and T7 (soak) remain outstanding. TT has a reproduced failure and a
partial RTC-enabled correction; durable/reboot-rollback support remains #14.
HKDF/full-handshake vectors, wider fuzzing, actual usrsock lifecycle faults,
representative stack measurements, SMP, and PROTECTED remain separate gaps.
Long-run/failure-mode work is tracked in
[#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5).

## C. Cross-implementation & upstream

- **The FLAT restriction ([#6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6))
  is RESOLVED** by moving into the kernel (the socket is kernel-owned). #6 is closed;
  this does not mean every board's BUILD_KERNEL/PROTECTED configuration was tested.
- **Bugs found in NuttX itself while porting** — good talk material, and separate upstream work:
  the `crypto/chachapoly` nonce (its own `crypto:` PR), the usrsock ioctl issues
  ([#10](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/10)), and the
  `CONFIG_RTC_HIRES` cxd56 regression
  ([#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)).
- **dev@ design consensus** before PRs is tracked in
  [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3); the draft is
  [dev-list-proposal.md](dev-list-proposal.md).

## D. For the Community Over Code talk — what to put to the audience

The talk's "contributing back" close can invite review on a small subset of §A that a room of
ASF/embedded developers is well placed to judge:

- the **ioctl ABI shape** (A1) — fixed-size vs netlink, for a constrained target;
- **kernel-owned socket + thread lifecycle** (A3) — review `d_lock`, queued output, and recovery;
- **the `crypto:` nonce fix** (A6) — review the shared fix and completed KAT evidence;
- and an open ask: **which negative/soak tests** (A8/§B) matter most before merge.

Keep the framing neutral (no GSoC), and don't present open questions as decided.
