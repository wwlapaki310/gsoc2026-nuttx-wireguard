# Handoff — what is left, how to do it, and what will waste your time

Written 2026-09-28 for whoever picks this up next (human or agent). The other
documents say *what is true*; this one says *what to do next and what will trip
you up*.

- Measured truth: [verification-matrix.md](verification-matrix.md). Its "Honest
  remainder" section is the short version of this file.
- Priority and acceptance conditions: [remaining-work.md](remaining-work.md).
- Narrative log: [Discussion #18](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/discussions/18).
- Environment and pinned revisions: [reproduce.md](reproduce.md).
- Dev loop: `scripts/kdev.sh sync|configure|build|style|test kernel/<script> [lines] [args...]`.

## Ground rules

These are not preferences; breaking them has cost real rework.

1. **Upstream posting is the maintainer's job, not yours.** Issues, PRs and dev@
   posts to `apache/nuttx` and `apache/nuttx-apps` are made by the repository
   owner. Prepare branches, patches and drafts; do not push forks or open PRs.
2. **Never write "all verified" or "merge-ready".** Every claim in the matrix is
   paired with what it does *not* show. Keep that shape. A result whose limits
   are not stated is worse than no result, because it stops anyone else looking.
3. **Do not reformat `wg_x25519.c` / `wg_x25519.h`.** They are adapted from the
   STROBE project under MIT and the algorithm body keeps that formatting so it
   stays diffable against its source. nxstyle skips them via `g_white_files[]`
   in `tools/nxstyle.c`.
4. **No Wi-Fi credentials in the repository.** They go in as build arguments or
   at run time. `hw-images/` is gitignored because built firmware embeds them.
   Redact serial logs before tracking them.
5. **A test that cannot fail is not a test.** Several checks here were wrong in
   exactly that way before being fixed (see the traps below). When you add one,
   ask what it would print if the feature were removed.

## Task 1 — rebase both forks and re-verify (do this first)

The fork bases are old: **nuttx is 255 commits behind `upstream/master`**, apps
is 30. Nothing can be submitted from that base, and a rebase can break things
quietly — `netdev_upperhalf.c`, `net/netdev/netdev_ioctl.c` and `mm/iob/Kconfig`
are all touched by the driver and all move upstream.

- `net-wireguard` HEAD `b230ee4876`, base `c95c546c`
- `system-wg` HEAD `691311a4`, base `73a9c9a6`

**Do:** rebase each branch onto current `upstream/master`, keeping the commit
structure (see Task 2). Then re-run, in this order, and record each result:

| | how |
|---|---|
| style | `kdev.sh style` — must end "All checks pass" twice plus "no key material in .config" |
| T1 | `kdev.sh test kernel/verify-sim-wg-runtime.sh` |
| TF, TV, TN, T3 | `verify-sim-wg-ioctl.sh`, `verify-sim-wg-kat.sh`, `verify-sim-wg-negotiation.sh`, `verify-sim-wg-multipeer.sh` |
| TR | `verify-sim-wg-replay.sh` (a–d; the out-of-window arm takes a few minutes) |
| TZ, #17 | `verify-sim-wg-zeroize.sh`, `verify-sim-wg-keyfile-faults.sh` |
| T6, TS | `build-knetnsh.sh` then `verify-knetnsh-wg.sh`; repeat with `build-knetnsh.sh rv-virt:knetnsh64_smp` |
| TP | `build-pnsh-wg.sh` then `verify-pnsh-wg.sh` |
| T8 | `build-cmake-wg.sh` |

**Acceptance:** every one of the above passes on the rebased branches, and the
matrix's "Target under test" paragraph names the new base and HEADs. T7, TH, TE
and TT do **not** need repeating for a rebase unless the driver's send/receive
path changed; say which you re-ran and which you did not.

## Task 2 — assemble the PR series

Planned shape, from [pr-drafts.md](pr-drafts.md):

1. **`crypto:` PR** — the `chacha20poly1305` u64-nonce fix **and** the
   chachapoly + xchacha vectors now sitting in `crypto/testmngr.c` /
   `testmngr.h`. Those vectors are currently in the tree but must travel with
   *this* PR, not the driver one. Verified at boot: `up_cryptoinitialize: crypto
   test OK` under `CONFIG_CRYPTO_ALGTEST`.
2. **PR-K1 (`apache/nuttx`)** — the driver, its public header, Kconfig/Make/CMake
   wiring, the `mm/iob/Kconfig` default, Documentation, and the one-line
   `tools/nxstyle.c` exclusion.
3. **PR-A1 (`apache/nuttx-apps`)** — `apps/system/wg`.

**PR-A1's style check depends on PR-K1 landing first**, because the nxstyle
exclusion for the vendored X25519 lives in the nuttx repository. Say so in the
PR description; it is already in the draft.

**Acceptance:** each commit builds on its own (`git rebase --exec` with a sim
build is enough), and `./tools/checkpatch.sh -g <range>` passes over the actual
commits. Note that this has **not** been run yet the way CI does it — everything
so far used `-f` over the files, which is stricter per file but says nothing
about the patch form (renames, whitespace in the diff).

## Task 3 — close two named gaps that are reachable in the simulator

Both are listed in the matrix as narrowness, with the exact reason.

**(a) TZ: `next_keypair` and the handshake fields were never non-zero when
sampled.** `verify-sim-wg-zeroize.sh` forces one rekey so `prev_keypair` really
holds a key, and that row is real. `next_keypair` only holds a key in the window
between a responder installing it and it becoming current, and
`wg_start_session()` wipes the handshake at key derivation — so both read as
"held no key while up, not exercised". Arrange to sample inside that window (the
`CONFIG_NET_WIREGUARD_DEBUG_*` hooks are the obvious lever) and either confirm
the clearing or replace the note with what you found.

**(b) TI: the IOB *blocking* path is untested.** In
`verify-sim-wg-iob-exhaustion.sh` the pool reaches zero free but `nwait` never
rises, so what is exercised is "allocation fails, packet is dropped". First
settle whether this driver can ever block on an IOB at all — if every allocation
on its paths is a try-variant, then the row should say "not reachable" rather
than "untested", which is a better answer than a test. Only if it is reachable
is there something to exercise.

**Acceptance:** the matrix rows for TZ and TI no longer contain an unexplained
gap — either a measurement, or a statement that the path does not exist with the
code reference that shows it.

## Task 4 — make the hardware work one command each

These are blocked on hardware, not on thinking, so the useful work is to have
everything ready for the moment a board is attached. Mirror the Spresense
scripts.

- **ESP32-S3 resource measurement** — the equivalent of
  `verify-spresense-resources.py`. Native Wi-Fi rather than usrsock, so its
  numbers will differ and are worth having. Needs a `--measure`-style image for
  that board (`STACK_COLORATION` + procfs).
- **BUILD_PROTECTED on silicon** — `esp32s3-devkit:knsh` is a PROTECTED config.
  TP passed on QEMU's MPU model; this would be a real MPU on a different
  architecture, which is the gap that matters.
- **SmartFS rename and power-cut durability (#17)** — the last open item there.
  Needs switchable power; a DTR reset is not a power cut. Write the procedure
  even if you cannot run it.

**Acceptance:** a script that fails with a clear message when the board is
absent, and that a person with the board can run without reading the source.

## Task 5 — presentation

The deck is 26 slides: `docs/presentation/coc-glasgow-slides.html`. It is current
as of 2026-09-28, including the verification slide ("Seven places it has to
hold"), the "A working tunnel is not evidence" slide, and the design-call slide
carrying the measured timestamp pair. What is left is **D3, a rehearsal**, which
needs the presenter: time it, and decide the fallback if the network misbehaves
(recording or static logs). Do not put anything in the deck that is not in the
matrix.

## Traps that have already cost time

Read this list before debugging anything.

1. **`tapwg` outlives the QEMU tests.** `verify-knetnsh-wg.sh` and
   `verify-pnsh-wg.sh` create a TAP holding `10.0.0.1`. If it is left behind, the
   next *sim* test cannot give that address to its own `tap0` and reports "tunnel
   configured at runtime did not carry traffic" — which looks exactly like a
   driver regression. Both cleanups now delete it; if you see that failure,
   check `ip -brief addr` first.
2. **`/proc/iobinfo` is instantaneous, not cumulative**, and `nthrottle` is how
   many a throttled caller may *still* take — so a large value means the pool is
   healthy, not that throttling happened. There is no event counter. Sample
   during the load or you measure nothing.
3. **Read the serial console until the prompt returns.** A fixed wait truncates
   `ps` and `free`, and a truncated sample parses as "no data", which a careless
   check reads as passing.
4. **Under usrsock, a board-side `ping` produces no payload datagram** — the
   board's `socket()` goes to the GS2200M, not the kernel stack. Drive traffic
   from the peer; the board's ICMP *replies* are what exercise the send path.
5. **A silently unapplied patch.** `build-spresense-kernel.sh` fails loudly if a
   patch does not apply, because a silent "pattern not found" is what caused a
   wrong diagnosis of the cxd56 boot hang (the real cause was issue #9, the
   `CONFIG_RTC_HIRES` regression, not a Docker cache quirk).
6. **`rcS` goes through the C preprocessor.** Use `/* */` comments; `CONFIG_*`
   macros expand inline; shell `#` comments break the build. And NSH's
   `CONFIG_NSH_NESTDEPTH` is 3 — three-deep `if` aborts the whole script with
   "if: nesting too deep", taking the diagnostics you wanted with it.
7. **`olddefconfig` keeps a value that is already written.** The `mm/iob/Kconfig`
   default for `IOB_NCHAINS` only takes effect on a fresh evaluation; use
   `kconfig-tweak -u` first when checking it.
8. **CMake refuses to configure over a Makefile build.** `make distclean` first.
9. **`kdev.sh sync` applies the fork's working-tree diff**, so uncommitted
   modifications to tracked files do sync — but **new untracked files do not**.
10. **The sim's first handshake is occasionally flaky.** Poll, do not single-shot.
    And recovery after a *timed-out* stop is `down; down; up`, not `down; up`,
    because `IFF_UP` stays set when `d_ifdown` returns an error.

## What is deliberately not being done

Do not reopen these without a reason that is not already written down.

- **#14 durable range reservation.** Designed in
  [tai64n-design.md](tai64n-design.md), deliberately unimplemented: NuttX offers
  no storage durability contract to build it on (`hostfs_sync()` calls a void
  `host_sync()` and returns OK, so a successful write in the simulator says
  nothing about power loss). The comparison and decision are in
  [tai64n-decision.md](tai64n-decision.md), and the hardware measurement is in
  [evidence/spresense-timestamp-2026-09-28.md](evidence/spresense-timestamp-2026-09-28.md).
  **#14 stays open by decision.**
- **A full-handshake KAT.** Linux kernel WireGuard *is* the reference, and T1, T6
  and TP complete real handshakes against it on three configurations. A
  self-generated transcript would only add failure localisation, which the
  per-primitive and per-derivation KATs now provide.
- **Reaching curve25519 through cryptodev for `wg pubkey`.** It exists
  (`CRK_DH_COMPUTE_KEY` → `swcr_dh_make_common`), but the handlers compile only
  under `CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO`, which depends on `CRYPTO_SW_AES` —
  so one scalar multiplication would drag the whole software cipher suite into
  the image. See A5 in [open-questions.md](open-questions.md); the earlier
  recorded reason for vendoring was wrong and has been corrected.
