# Configuration recovery and session-lifetime follow-up

Date: 2026-09-28. Actual local runs, not reports copied from another agent.

## Target

- NuttX `net-wireguard`: `b230ee4876`, unchanged by this follow-up.
- apps `system-wg`: `37f04cff`, parent `691311a4`.
- Runner: isolated Docker container `wg-review-test`, snapshot of `wgdev` with
  the changed apps source copied in and rebuilt. No board or AP was operated.
- `.config` SHA-256:
  `d083b215e5141ab4b432f9e884fc33b4dcdec4057d7df74b047594b17e9b903c`.
- `wireguard.c` SHA-256:
  `374bddf5e9f41c919c9fc9bd6b39680b6ef8d18c2f1927f969f2bee726c29e13`.
- `wg_main.c` SHA-256:
  `3c93f7aee9fcfa6009320d0ccf6f5adf67b647a959d0d9751c3edd918f4feb33`.
  Both source hashes match the local fork files.

## Results

| Check | Result | Limit |
| --- | --- | --- |
| sim incremental build | exit 0 | Not a rebase or all-target build |
| `WG_CONTAINER=wg-review-test scripts/kdev.sh style` | Both checks pass; no key-shaped material in config | File checks, not whole PR-series CI |
| `checkpatch.sh -p recovery.patch` | PASS on a temporary parent-source tree | This one follow-up patch only |
| `verify-sim-wg-runtime.sh` (T1) | PASS, including save/down/load/up | Host Linux peer, not hardware |
| `verify-sim-wg-ioctl.sh` (TF) | PASS | Existing negative-case suite |
| `verify-sim-wg-kat.sh` (TV) | PASS | Primitive/derivation KATs, not full transcript KAT |
| `verify-sim-wg-keyfile-faults.sh` | PASS | sim VFAT ENOSPC, recovery and background writers; does not force writer overlap |
| `test-wg-file-publish.py` | 11 cases PASS; negative-control mutant rejected | Actual helpers compiled on host, modeled VFS faults, not SmartFS media |
| Expanded `verify-sim-wg-zeroize.sh` (TZ) | PASS | Named fields in a live sim process; controlled network schedule |

The publisher harness checks first-save/update, destructive and nondestructive
rename failures, unreadable input, short backup write, close failure, occupied
backup, missing staged file, read error, and two real host processes with the
first held inside rename. The second cannot replace the reserved recovery copy.
It also checks exclusive mode-0600 staging, path-length rejection, and repeated
key updates without accumulating duplicate PrivateKey lines. Ignoring rename
failure deliberately makes the oracle fail.

TZ now has three distinct observations:

1. No responder: handshake ephemeral-private/chaining-key/hash are nonzero,
   then all zero after down.
2. Normal traffic: the Linux handshake timestamp advances **121 seconds** with
   no peer deletion/re-addition or clock adjustment during the rekey phase.
   The current sending key changes; current and previous sending/receiving
   keys are nonzero, then zero after down. A re-up establishes fresh keys.
3. A UDP relay withholds Linux transport confirmation and NuttX initiation:
   the NuttX responder's next sending/receiving keys are observed nonzero,
   then zero after down. Actual Linux confirmation packets were withheld.

Static identity retention is intentional. The probe observes one raw copy
before/after the regular down; it is not an exhaustive forensic-erasure proof.

## Corrections and remaining work

- Rename-first was not safe against NuttX VFS failure. See
  [recovery semantics](../keyfile-correctness-review.md).
- The initial attempted reconstruction of the test container selected a legacy
  `wg` command and failed baseline setup. That run is excluded; the recorded
  runs used the existing kernel-WG environment snapshot and matching source
  hashes. The original `wgdev` source tree was not replaced.
- [IOB audit](../iob-wait-audit.md): the direct buffered UDP/RX allocation paths
  are nonwaiting. A real blocked usrsock send remains untested by the delay hook.
- No power-cut proof, hardware rerun, AP-loss test, days-long run, or new
  PROTECTED/SMP/CMake result is claimed. #14's platform-clock decision remains.
- Rebase onto current upstream, split the full crypto/driver/apps PR series,
  and validate that final series remain submission work. Fork publication is
  still the owner's action; this repository carries the follow-up patch.
