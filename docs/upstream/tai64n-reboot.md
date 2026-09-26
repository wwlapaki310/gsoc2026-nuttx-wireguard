# TAI64N reboot reproduction (#14)

## Scope

This test isolates NuttX-initiated handshakes after process restart, with the
same private key and an unchanged Linux WireGuard peer. It runs entirely on
the PC's Linux container/TAP network. Home Wi-Fi, a physical board, Internet,
and package downloads are not required once the build environment exists.

This document records the original failing baseline, where `wg_tai64n()` used
`clock_systime_timespec()`. The follow-up [partial correction](tai64n-design.md)
uses realtime with a same-boot high-water mark; `wg_now()` is unchanged.
Persistent timestamp allocation, power-loss safety, and cross-reboot rollback
remain design work; this test does not complete the four TT scenarios.

## Reproduce

Use a dedicated container/network namespace with `NET_ADMIN`, `/dev/net/tun`,
Python 3, `ip`, `wg`, and a built `/opt/nuttx/nuttx`. Do not run alongside another
sim in the same network namespace. The script refuses existing `tap0` or
`wgreboot0`; it does not install tools or kill unrelated processes.

Build `sim:wireguard` with:

```text
CONFIG_NET_WIREGUARD_DEBUG_TAI64N=y
CONFIG_SYSTEM_PING=y
CONFIG_NET_BINDTODEVICE=y
# CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL is not set
# CONFIG_NET_WIREGUARD_DEBUG_TX_STALL is not set
```

The default-off timestamp option logs the generated, pre-encryption timestamp,
not keys. Never enable it in a production build. The packet observer sees
WireGuard message types and sender indices, **not decrypted wire timestamps**.
After the realtime correction, use normal regression mode with `CONFIG_RTC=y`.
`--expect-rejection` is for the old implementation or a deliberately reset
clock (for example the RTC-disabled negative control), not the expected outcome
of a correctly retained realtime clock.

```sh
python3 scripts/kernel/verify-sim-wg-reboot.py \
  --output /tmp/wg-reboot-evidence --expect-rejection
python3 -B scripts/kernel/test-wg-reboot.py
```

The evidence directory must not already exist. Keys live in a temporary private
directory; saved consoles redact both private keys. `report.json` records packet
metadata, timestamp logs, handshake counters, and checked NSH command statuses.

## Oracle

1. Boot NuttX and wait until approximately 70 seconds of uptime before `wg up`.
2. Require one unambiguous initial timestamp, a Linux handshake, and a successful
   NuttX-originated tunnel ping. Allow 20 seconds of keepalives to settle the
   Linux reply's pending retry timer.
3. Kill only the NuttX process. Start it again with the same private key, leaving
   the Linux interface, peer, key, and remembered handshake state intact.
4. Observe 30 seconds. Reproduction requires at least two distinct NuttX
   initiation sender indices, lower generated timestamps, no Linux response,
   unchanged Linux latest-handshake, and a target ping with zero replies.
5. Keep the peer unchanged and wait for the new uptime to exceed the old
   timestamp. Require a newer handshake, a higher generated timestamp, and a
   successful target ping as a positive recovery control.
6. Any Linux-originated initiation invalidates the run: it could conceal the
   NuttX initiator defect. Interface identity must remain unchanged.

`--expect-rejection` returns zero with **REPRODUCED** only when both rejection
and catch-up recovery are observed. It never labels the defect PASS. Without
that flag, the test requires reconnection within the 30-second observation
window and returns nonzero for the known defect. Host-side unit tests check
this distinction. Ping is bound to `wg0` with `-I` and checked by its receive
count, not only exit status. An unbound ping can reach the Linux tunnel address
through the underlay when the tunnel is unavailable; it is not a valid oracle.

## Observed result (2026-09-23)

Diagnostic mode: **REPRODUCED**, exit 0. This confirms the defect, not a fix.
An independent run without `--expect-rejection` also observed six unanswered
initiations, failed bound ping, and subsequent catch-up recovery. It returned
**FAIL**, exit 1, as required for a regression test of the unfixed behavior.

| Observation | Result |
| --- | --- |
| Before restart | `40000000000000501a39de00` (uptime 70.44 s) |
| First timestamp after restart | `400000000000000a26be3680` (uptime 0.65 s) |
| First 30 seconds | 6 distinct initiations, 0 Linux responses, latest-handshake unchanged |
| Bound tunnel ping during rejection | 0 replies |
| Catch-up timestamp | `40000000000000551017df80` (uptime 75.27 s) |
| Linux response after restart | 75.303 s after process launch |
| Recovery | New handshake and bound tunnel ping success; Linux did not initiate |

Source baseline: NuttX `66b7403c8a` plus the test-only timestamp logger/Kconfig;
apps `24b3f311`. Linux kernel `6.6.87.2-microsoft-standard-WSL2`, wireguard-tools
`v1.0.20210914`. Dedicated `wg-codex-locking` container; existing `wgdev` was
not modified. Build and timestamp-source checkpatch succeeded. Four host-side
evidence-parser/verdict tests passed. The compiler still emitted existing
`wg_x25519.c` warnings; this is not a warning-free build claim.

Local artifacts (ignored by Git):
`output/tai64n-reboot-validation/reproduced/report.json`, redacted boot logs,
`regression/report.json` (under the same validation directory), `sim.config`,
and `build.log`. Exploratory runs are retained separately and
are not counted as passes. The table above preserves the key evidence for
reviewers who do not have the local artifacts.

Both runs cleaned up their sim processes and test interfaces. The dedicated
container was stopped after collecting the evidence.

## Limits

The test demonstrates behavior with local Linux WireGuard, not a physical
board's RTC or storage guarantees. The timestamp/recovery correlation and the
unchanged peer are evidence for the replay check; no Linux internal rejection
reason is instrumented. A repaired implementation may need different diagnostic
logging if it no longer calls this timestamp function.

An initial exploratory run without the settling interval was invalidated:
Linux initiated at about 14 seconds after the restart and restored the tunnel.
That is a masking path, not proof that the initiator timestamp defect is fixed.

A second exploratory run observed six initiations without responses and no
new Linux handshake, but an unbound target ping still succeeded. No transport
data packet accompanied that ping in the WireGuard capture. The final harness
therefore requires `NET_BINDTODEVICE` and uses `ping -I wg0`; the exploratory
run is not counted as a successful reproduction.
