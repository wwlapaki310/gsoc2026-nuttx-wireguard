## In-kernel WireGuard: real Wi-Fi tunnels on both boards

The September 26 hardware test report is an important milestone: the
**in-kernel driver**, not only the earlier apps implementation, establishes
tunnels to the official Windows WireGuard client on both boards.

| Board / backend | Reported result |
| --- | --- |
| ESP32-S3 / native Wi-Fi | Tunnel ping 4/4 |
| Spresense / GS2200M via usrsock | Recent handshake, 496 B TX and RX, tunnel ping 6/6, approximately 5-8 ms RTT |

Spresense restored its existing configuration from SmartFS using `wg setconf`;
the Windows peer configuration did not need to change. The kernel image is
currently a serial-configured demo; automatic headless startup is still a
follow-up, not a demonstrated capability of this image.

### What was reviewed

The report was recorded in [commit 76ccf2f](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/commit/76ccf2f271137e6196b4a563381bcf3f5b5e7206).
On September 27, repository/build-artifact inspection confirmed that the
retained Spresense tree is based on `nuttx-13.0.1`, has `NET_WIREGUARD`,
`SYSTEM_WG`, `NET_USRSOCK`, `BUILD_FLAT`, and `IOB_NCHAINS=8`, and contains the
queued-output driver. The saved `spresense-kernel-wg-img.spk` matches the
container's generated image, SHA-256:

```text
aedb710ae6997b818e2d3524cbc39b392454e10921f87d166c6fa3ac0b42ea82
```

This review did not rerun the hardware test or read back board flash. The
handshake/ping measurements above are the hardware operator's reported results.

### What this does not establish

- It verifies **functional interoperability through real usrsock hardware**,
  not exhaustive concurrency safety. Blocked sends, concurrent configuration,
  timeout/reap recovery, buffer pressure, and Wi-Fi loss still need targeted
  tests on that backend.
- This is the in-kernel driver in a **FLAT build**, not a hardware demonstration
  of BUILD_KERNEL/PROTECTED isolation.
- The retained Spresense source still uses uptime for TAI64N; it does not
  contain the separate realtime/high-water correction. The handshake initiator
  was not established by the supplied report. Consequently, this result does
  **not** close [#14](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/14).
- A plain fresh-tree Spresense build reportedly also hangs during early boot,
  while a cached build boots. This narrows the investigation but does not yet
  prove the exact cause. The retained tree has additional RTC, clock, and
  GS2200M changes; those differences must be captured and reconciled. A farapi
  warning also appears in a working image, but that is not a blanket guarantee
  that the mismatch is harmless for all features.

### Next priorities

1. Preserve a reproducible source/patch/config/toolchain/image manifest and
   redacted serial/ping evidence, then add the formal Spresense kernel Docker
   stage.
2. Add headless kernel startup and rehearse cold boot, Wi-Fi readiness/failure,
   and recovery for the presentation demo.
3. Keep actual usrsock stall/lifecycle tests, NuttX-initiated reboot tests,
   and long-running soak separate from the connectivity milestone. Integrate
   the IOB configuration fix into the driver submission.

For the talk, the supported claim is: **"The in-kernel driver establishes real
Wi-Fi tunnels on both boards, including the Spresense GS2200M/usrsock path."**
That is a substantial step forward without implying that all merge blockers
or concurrency failure modes have been resolved.
