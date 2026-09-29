# Rebased upstream submission-series validation

Date: 2026-09-29. These results were run locally by Codex against the rebased
candidate, not copied from an earlier report.

## Source identity

| Repository | Upstream base | Candidate |
| --- | --- | --- |
| NuttX | `68dd87f4df9ad1e19240136b931867efbcbc23d0` | `8defcefa947645d2245a320dc2d0e0c2eb8ce6cd` |
| apps | `b66303e26aa537dd74d6abaeeeded81c151a7e35` | `c039b232e5fcf181ac4d16b9231cdc42a025d45c` |

Both development series rebased without conflict. The submission series was
then squashed into three review units: crypto prerequisite, NuttX driver, and
apps command. Exported patches applied cleanly to detached clean base worktrees;
the resulting trees matched the candidate trees with `git diff --exit-code`.
Original development branches were preserved and no fork was pushed.

## Results

| Check | Result | Scope / limitation |
| --- | --- | --- |
| Crypto commit alone | PASS: sim build, boot, `crypto test OK` | No driver present; startup ALGTEST only |
| Driver commit without apps `wg` | PASS: sim build, `wireguard_initialize` linked, `wg0` registered | Registration, not tunnel operation |
| File/patch style | PASS: `checkpatch.sh -g` says "All checks pass" for both actual commit ranges; codespell and encoding checks pass | NuttX `68dd87f4df..8defcefa94`; apps `b66303e26a..c039b232e5` |
| CI message check | `checkpatch.sh -m -g` fails only for missing `Signed-off-by` on the three commits | Owner must review and certify; no identity was invented. Re-run after signing changes the SHAs |
| Combined sim build | PASS | `.config` SHA-256 `14d5b1c7325230cdec190a5d6f5af2f426297b1d27bd4eff1ea7181e8cfcaf73` |
| T1, TF, TV, TN, T3, TR | PASS | Linux host peer / sim TAP, sequential isolated suite |
| Expanded TZ | PASS | Natural rekey plus nonzero handshake/next-key observations; sim memory only |
| Keyfile fault suite | PASS | sim VFAT/ENOSPC and background writers, not a power cut |
| Extracted publisher suite | 11 cases PASS; broken mutation rejected | Host-modelled faults, not SmartFS media |
| BUILD_KERNEL, 1 CPU | PASS: two-way tunnel; load/reconfigure/down/up recovery | rv-virt QEMU, not hardware |
| BUILD_KERNEL, SMP 4 CPUs | PASS: same runtime scenario | One scripted interleaving, not race exploration |
| BUILD_PROTECTED | PASS: build and runtime tunnel across MPU split | QEMU MPU model, not silicon |
| CMake/Ninja T8 | PASS: driver and apps objects verified | sim build only |

The sim suite runs these scripts in order and verifies the source revisions do
not change between tests: runtime, ioctl negatives, primitive/derivation KAT,
negotiation negatives, multipeer, replay/cookie/fuzz, zeroization/natural rekey,
and keyfile faults. Raw logs are intentionally not committed because they carry
ephemeral test keys.

One excluded run used the wrong default config path (`/data/wg0.conf`) after a
driver-only configuration step. Tunnel traffic passed, but persistence failed
because `/data` was not mounted. The suite now requires `/tmp/wg0.conf` before
starting. A later accidental parallel PROTECTED/CMake build also invalidated
that PROTECTED build attempt; both configuration-changing builds were rerun
serially. Neither excluded run is treated as product evidence.

## Review findings closed during assembly

- The old commit arrangement mixed crypto KATs into the driver commit. They now
  travel with the nonce prerequisite and that commit builds/boots alone.
- Full-range checkpatch exposed pre-existing style in touched shared files and
  two vendored-header comment misspellings. These were corrected without
  changing or reformatting the vendored X25519 algorithm body.
- `kdev.sh style` now propagates pipeline failures. `kdev.sh sync` now refuses
  to reset/apply when container HEADs do not equal the fork merge bases.
- The PROTECTED build helper accepts current `nuttx_user` and older
  `nuttx_user.elf` output names.

## Still open

This is a stronger submission candidate, not a merge-ready declaration.
Outstanding conditions remain: #14's reliable platform-time contract on boards
without retained time; SmartFS power-cut behavior; real AP loss/backend blocking;
day-scale operation; hardware PROTECTED; maintainer agreement on ABI/design;
and owner `Signed-off-by`, fork publication and PR submission. Earlier hardware
results were not rerun after this rebase because the boards/controlled AP were
not available in this run.
