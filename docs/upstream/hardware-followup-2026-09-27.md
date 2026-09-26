# Hardware report review (2026-09-27)

Published summary: [Discussion #16](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/discussions/16).

## Accepted result and scope

The 2026-09-26 report supports **functional in-kernel tunnel interoperability
on both boards**, including the real Spresense GS2200M/usrsock backend:

| Board | Reported observation |
| --- | --- |
| ESP32-S3, native Wi-Fi | Windows-to-tunnel ping 4/4 |
| Spresense, GS2200M/usrsock | `wg0` RUNNING, recent handshake, TX/RX 496 B each, Windows-to-tunnel ping 6/6 at approximately 5-8 ms |

The Spresense configuration was restored from SmartFS with `wg setconf`, with
the Windows peer unchanged. This is evidence for configuration restoration
and functional interoperability, not yet a controlled NuttX-initiated reboot
test. The report does not establish which side initiated the handshake.

This review inspected the repository and retained build artifacts, **not a
fresh serial/hardware rerun**. Full raw serial and Windows ping transcripts
were not located in the inspected tracked evidence. Packet counts above are
attributed to the supplied test report and commit `76ccf2f`.

## Independently checked artifacts

- Commit `76ccf2f` updates the verification matrix and presentation with the
  two-board result. Remote wiki HEAD is `18f0418761c26ebf693bf92e764dbb75e2c67bd7`;
  this verifies the reported push, not every sentence of the wiki content.
- Retained container `wgspr` has NuttX base
  `cec617dfbce984bc0bf242922c3c4d306238c4de`, exactly tagged `nuttx-13.0.1`.
  Its apps base is `be1ae4e896aa0739e00efdc3478f0d01175b82dc`, with local changes;
  these are ported build trees, not clean checkouts of the current fork HEADs.
- Selected config: `BUILD_FLAT=y`, `NET_WIREGUARD=y`, `SYSTEM_WG=y`,
  `NET_USRSOCK=y`, `IOB_NCHAINS=8`, `RTC=y`. FLAT runs the in-kernel driver,
  but does not establish KERNEL/PROTECTED memory isolation on this board.
- `hw-images/spresense-kernel-wg-img.spk` and `/opt/nuttx/nuttx.spk` in `wgspr`
  have the same SHA-256:
  `aedb710ae6997b818e2d3524cbc39b392454e10921f87d166c6fa3ac0b42ea82`.
  This matches the retained artifacts; it is not a readback of board flash.
- Current container `.config` SHA-256:
  `20b04c296b8058cb5925d8149f017d0350a9de8487c4a73739f0f712c386cade`.
  Its relation to the exact build must still be preserved in a build manifest;
  hashing the current file alone cannot prove it was never changed afterward.
- The retained `wireguard.c` contains `wg_flush_tx`; SHA-256:
  `cde1a2c457c72bfb4cd99b37ef8ec520a7f2ccf8d814d25c7080b4c76a41f5c7`.
- Its `wg_crypto.c` still builds TAI64N from `clock_systime_timespec`.
  The separate realtime/high-water correction is **not in this inspected
  driver source**. Hardware PASS must not be cited as validation of that fix.

## Claims to narrow

1. **Usrsock safety:** normal handshake/ping exercises the real backend, but
   does not force a blocked `psock_sendto`, concurrent reconfiguration, stop
   timeout/reap, IOB exhaustion, or Wi-Fi loss. Say "functional usrsock
   interoperability verified", not "queued-output concurrency safety proven".
2. **Early-boot root cause:** failure of a plain fresh-tree control is evidence
   against WireGuard being a necessary trigger. It does not identify the exact
   cause or establish equivalence of cached and fresh builds. The retained
   tree also changes `cxd56_rtc.c`, `clock_systime_timespec.c`, and `gs2200m.c`
   (including SPI timing). Preserve those diffs and compare toolchain/config/
   source/build cleanliness before claiming a specific build-cache cause.
3. **farapi warning:** its presence in a working image shows it is not sufficient
   to prevent this tested boot/tunnel. It does not prove the mismatch harmless
   for every feature, including GNSS.
4. **Merge readiness:** T5 connectivity PASS does not close #14, T7 soak,
   fault-injected usrsock lifecycle tests, or hardware KERNEL/PROTECTED checks.

## Next work

1. Preserve exact source/patch/config/toolchain/image hashes plus redacted
   flash, boot, `wg show`, and Windows ping logs for the working image. Record
   handshake direction and distinguish the apps and in-kernel binaries.
2. Build a reproducible kernel Spresense Docker stage from that manifest.
   Reconcile the RTC/clock/GS2200M differences rather than relying on a cache.
3. Add headless kernel `rcS`, then test repeated cold boot, Wi-Fi-not-ready,
   configuration failure, and recovery. Do not embed credentials in public logs.
4. Exercise actual usrsock send stalls and concurrent control/stop recovery,
   and run board-initiated same-key reboot tests for #14 independently.
5. Integrate and review `IOB_NCHAINS` configuration handling in the driver PR;
   a working default alone does not rule out an explicit incompatible override.

Recommended presentation wording: **"The in-kernel WireGuard driver establishes
real Wi-Fi tunnels on both ESP32-S3 and Spresense, including GS2200M/usrsock.
Concurrency fault testing and reboot-time persistence remain separate work."**
