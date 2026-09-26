# wg key/config persistence: correctness follow-up

Tracking: [Issue #17](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/17).

Reviewed 2026-09-27 against local nuttx-apps `24b3f311`,
`system/wg/wg_main.c`. Static inspection only; fault injection is not yet run.

## Findings

1. `wg_record_private_key` (around lines 534-588) returns void. Failed `fopen`
   silently returns; write, `fclose`, and `rename` failures are not propagated.
   The `set private-key` path (around lines 728-742) first updates the driver
   and then invokes this helper, retaining the successful ioctl result. Thus
   a command may report success although the configured key was not saved.
2. Both this helper and `wg_cmd_saveconf` (around lines 1091-1133) call
   `unlink(path)` before `rename(tmp, path)`. Failed replacement or power loss
   between those operations can remove the previous configuration. Saveconf
   reports rename failure but cannot restore the already-deleted original.

The driver deliberately does not return the private key. Runtime/file
divergence therefore matters for restart and subsequent save operations.
This is independent of whether the write-only key ABI is accepted upstream.
It is also separate from the durable timestamp design in #14.

## Fix applied 2026-09-27 (findings 1 and 2)

In the `system-wg` fork, `system/wg/wg_main.c`:

- `wg_record_private_key` now returns `int`. `fopen`, write (`ferror`),
  `fclose` and the replacement are all checked, and the temporary file is
  removed on every failure path.
- `wg set private-key` no longer reports success when the device accepted the
  key but it could not be persisted: it prints that the running key will be
  lost on restart and exits nonzero.
- New `wg_replace_file()` is shared by the key recorder and `saveconf`. It
  tries `rename()` first, so the previous file survives a failed replacement.
  Only if that is rejected does it fall back to unlink-then-rename — kept
  because a target filesystem may refuse to rename onto an existing name —
  and if that retry fails it reports that the original is gone and where the
  new content was left, instead of deleting both.

Verified: `sim:wireguard` builds clean, and T1
(`verify-sim-wg-runtime.sh`, which drives `set private-key`, `saveconf`,
down/up and `setconf`) passes, including "configuration survived a down/up
cycle through a file".

**Fault injection added:** `scripts/kernel/verify-sim-wg-keyfile-faults.sh`
makes the temporary path a directory so the save cannot open it, and asserts,
via `nsh-status.py` exit codes and the interface public key in `wg show`:

- `wg set private-key` exits **1** and says the running key is not stored;
- the device holds the new key while the file still holds the old one — the
  divergence the warning is about — and `setconf` brings the old key back,
  proving the **previous configuration survived** the failed save;
- `saveconf` under the same fault exits **1** and leaves the file intact;
- both recover once the fault is removed.

All five checks pass. Together with the T1 regression this covers the
open-failure and failed-replacement cases.

**Still not done:** ENOSPC/short write and close/flush failure injection, two
concurrent writers, interruption at each replacement boundary, and the
filesystem durability question — SmartFS `rename`/overwrite semantics and
power-cut behaviour are still unaudited, so no power-loss claim is made.

## Required work

- Propagate open/write/flush/close/rename failures; never report fully saved
  configuration when the runtime update succeeded but persistence failed.
- Define runtime/file transaction ordering and partial-success recovery. Do
  not assume the old key can be read back from the driver for rollback.
- Preserve the old file until a replacement is safely committed. Audit the
  actual target filesystem's rename/overwrite and durability guarantees;
  do not assume POSIX host behavior automatically holds on SmartFS.
- Review temporary-file ownership, permissions, concurrent writers, path
  truncation, and cleanup alongside the replacement implementation.
- Distinguish ordinary write-error safety from power-loss durability. Generic
  fsync success is not sufficient without checking the backend guarantees.

## Acceptance checks

- Inject open failure, ENOSPC/short write, close/flush failure, and rename
  failure. Require nonzero status and an accurate partial-state diagnostic.
- Show the previous valid configuration is retained after failed replacement,
  or document a tested recovery mechanism where atomic replacement is absent.
- Exercise successful private-key update, saveconf/setconf, down/up, and reboot
  recovery without printing private keys in published artifacts.
- Test two writers and interruptions at each replacement boundary on the
  supported storage backend. Do not claim power-cut safety from RAMFS tests.

Tracking context: #11 (implementation), #12 (operation/reproduction), #13
(write-only key design). This review does not reopen the closed license #7.
