# Open design & discussion points (talk + upstream)

One place for the development questions that are still open — for the Community Over Code talk
(what to put to the audience) and for the upstream review. This **consolidates existing
material** (the review brief's questions, the design doc's open items, the verification matrix,
and issues #3/#5/#6/#9/#10); it adds no new claims. Each item notes a *current position* where
one exists — a position, not a settled decision.

Sources: [in-kernel-review-brief.md §7](in-kernel-review-brief.md) ·
[in-kernel-design.md](in-kernel-design.md) · [verification-matrix.md](verification-matrix.md) ·
[in-kernel-plan.md](in-kernel-plan.md) §6.

## A. Design & ABI

1. **ioctl ABI shape.** Flat fixed-size ioctl vs a netlink-style interface for future growth
   (IPv6, more peers). *Position:* flat & pointer-free because PROTECTED/KERNEL copy the caller's
   struct directly; extend via flags. Open: is that acceptable long-term?
2. **RX = a kthread on `psock_poll`.** vs interrupt-driven or reusing an existing lower-half RX
   path. *Position:* a kthread mirrors `rpmsgdrv.c`; open: stall/priority-inversion risk.
3. **Kernel owns the UDP socket + thread — lifecycle.** ifup creates / ifdown tears down.
   *Position:* serialized by `net_lock`; open: any hole across ifdown / re-up / mid-failure
   (see design doc "open items").
4. **Private key write-only + config file as source of truth.** The get ioctl never returns the
   key; it lives in the user-side config file. Open: acceptable for backup/migration, or add a
   Kconfig-gated read-back for debugging?
5. **Vendored `wg_x25519.c` (MIT) in the `wg` command.** For offline genkey/pubkey. *Position:*
   kept because userspace can't call `crypto/` in PROTECTED. Open: could NuttX's own
   `CRYPTO_CURVE25519` be exposed to userspace and remove it?
6. **crypto nonce fix as a separate `crypto:` PR.** Order and granularity; add a u64-counter KAT
   to `crypto/testmngr.c`. Open: does `xchacha20poly1305` (cookie path, 24-byte nonce) need the
   same review? (Likely unaffected — nonce is passed as bytes — but untested.)
7. **Defaults & caps:** anti-replay window 2048, `MAX_PEERS`, `MAX_AIPS`, and the static memory
   they cost (~1.5 KB + 3 replay windows per peer). Open: right defaults/limits?
8. **Is the verification enough?** sim + rv-virt (BUILD_KERNEL) + hardware (apps) — plus the
   still-missing tests (§B). Open: what extra load/negative tests would a maintainer want?
9. **Commit split:** (a) `crypto:` nonce, (b) `net/wireguard` (ABI+driver+crypto+config+docs),
   (c) `apps/system/wg`. Open: split (b) further?
10. **Safety review:** thread-safety, endianness, memory safety — the places most worth a second
    pair of eyes (the stack-overflow bug is fixed; net_lock coverage of every `priv->wg` access
    is worth confirming).

## B. Verification / testing (still open)

From [verification-matrix.md](verification-matrix.md): **not implemented** — TF (ioctl
negatives), TV (KAT), TZ (zeroization), TE (entropy/cold-boot), TT (TAI64N/reboot), T7 (soak);
**not run against the kernel version** — T3 (multipeer), TN (negative interop), and **T5 (real
hardware)**. Long-run / failure-mode work is tracked in
[#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5). These are the honest gap
before PR-K1; none changes what is already shown (T1 + TR + T6).

## C. Cross-implementation & upstream

- **The FLAT restriction ([#6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6))
  is RESOLVED** by moving into the kernel (the socket is kernel-owned). #6 should be updated /
  closed with that outcome.
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
- **kernel-owned socket + thread lifecycle** (A3) — is the `net_lock` model the right one;
- **the `crypto:` nonce fix** (A6) — the KAT and whether xchacha needs the same look;
- and an open ask: **which negative/soak tests** (A8/§B) matter most before merge.

Keep the framing neutral (no GSoC), and don't present open questions as decided.
