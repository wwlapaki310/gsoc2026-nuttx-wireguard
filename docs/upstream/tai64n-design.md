# TAI64N correction and remaining persistence design (#14)

Source availability: the C correction is still an **uncommitted change in the
sibling NuttX fork**, not part of `66b7403c8a`. Publishing this document and its
test harness does not publish that driver change. The C unit runner requires
the corrected `drivers/net/wireguard/wg_tai64n.c`; a clean checkout of the old
fork revision cannot run it yet. Pin/publicize the matching source before
claiming an independently reproducible corrected build.

## Decision for this increment

Correct the demonstrably wrong clock source first, without adding a public
ioctl or filesystem I/O to the driver. This is a **partial correction**, not
closure of #14 or evidence that boards without time/storage support reconnect.

`wg_tai64n.c` now reads `nxclock_gettime(CLOCK_REALTIME)` and maintains an
in-memory high-water mark. The encoded value is the later of rounded realtime
and the next tick-sized slot after the previously allocated value. Serialization
remains 8-byte seconds / 4-byte nanoseconds, big endian, with the existing
`2^62 + 10` Unix-epoch offset. Protocol timers (`wg_now`) remain monotonic.

The allocator is shared across devices, so peer deletion, down/up, and private
key reconfiguration do not reset its same-boot history. A small mutex serializes
allocation across device locks; it acquires no device lock and performs no
socket or filesystem I/O. The lock order is device `d_lock` then timestamp
mutex. Test-only logging happens after releasing the timestamp mutex.

Invalid times, clock/lock errors, or encoding exhaustion return false without
changing the output. `wg_create_init` obtains a timestamp before modifying
handshake state and aborts on failure. Values remain consumed if later crypto,
queue admission, or sending fails. There is no fallback to uptime.

## Exact guarantee

- Within one boot: successful allocations strictly increase, including equal
  clock readings, valid clock rollback, and calls from multiple interfaces.
- Across boots: the platform must supply a new realtime value greater than
  the last value accepted by the remote peer. A sufficiently accurate retained
  RTC or trusted external time before initiation can provide this condition.
- `CONFIG_RTC=y` alone is not proof of a valid clock. An unset RTC, the build's
  `CONFIG_START_YEAR`, or a reset/rolled-back clock can still cause rejection.
  The current driver cannot distinguish a plausible-but-wrong date from a
  correct one; it does **not** claim to detect all unset clocks.
- A large forward jump followed by rollback can leave the in-memory value
  ahead of realtime. A subsequent reboot loses that protection. Very rapid
  restarts with a coarse RTC also need a separate bounded-recovery check.
- Concurrent use of the same private key on different machines is not made
  safe by this local allocator.

## Next increment: durable range reservation (proposal)

With neither a trustworthy retained clock nor durable state, a restarted
initiator cannot know the peer's remembered maximum. Random timestamps,
changing nanoseconds, or waiting a fixed few seconds do not establish order.
Those platforms need an explicit unsupported/responder-only policy or a
durable source; silently falling back to uptime is not a solution.

For platforms with writable nonvolatile storage, the proposed boundary is:

1. A control-plane component owns durable timestamp reservations, separate
   from the kernel networking fast path. Its backend must explicitly guarantee
   ordering/durability and report failures; a successful generic file write is
   not enough.
2. Reserve an exclusive upper bound **durably before** allowing the kernel to
   emit any value in the corresponding range. On restart, allocate above the
   previous reserved bound, even if most values were unused.
3. Kernel allocation is bounded by that reservation. Exhaustion, a realtime
   jump beyond the bound, or failure to renew must prevent new initiations;
   no wrapping, reuse, or implicit unbounded fallback. Existing data sessions
   and responder behavior need separate explicit lifecycle rules.
4. Bind the reservation domain to the local key identity and coordinate all
   its interfaces/writers. Reapplying a key, restoring a configuration, or
   racing two control processes must not allocate overlapping ranges.
5. Missing, corrupt, or rolled-back state for an existing key is not a fresh
   install. Fail closed until trusted time or explicit reprovisioning establishes
   a new safe baseline. A checksum detects corruption, not restoration of an
   older valid file; backup/clone rollback needs an explicit trust model.

This avoids flash writes on every handshake and allows gaps after power loss.
It is intentionally **not implemented yet**: storage backend selection,
reservation size/renewal, provisioning, public ABI, and failure observability
need agreement. No experimental timestamp ABI was added in this increment.

### Why not reuse wg0.conf immediately?

The existing `wg_record_private_key` write/rename path is not a durable monotonic
ledger. In addition, local inspection found that NuttX `hostfs_sync()` calls
the void `host_sync()` and returns OK; the sim implementation discards the
host `fsync()` return value. Thus a sim file-write success cannot by itself
prove flush-error handling or hardware power-loss safety. These are evidence
limits, not an unrelated hostfs change in this patch.

An accepted backend must define pre-provisioning, atomic/torn updates,
concurrent-writer exclusion, file/directory or media commit guarantees, and
what happens if power fails at every boundary. It must never publish a range
whose upper bound is not durable.

## Verification

`scripts/kernel/verify-wg-tai64n-unit.sh [nuttx-path]` compiles the actual
`wg_tai64n.c` with deterministic clock and pthread mutex shims, warnings as
errors, and UBSan. It checks byte order, tick truncation, equal readings,
rollback, nanosecond carry, forward jumps, clock/invalid-time failure atomicity,
and 4,000 concurrent allocations. It does not simulate reboot persistence or
prove NuttX SMP locking behavior.

The local Linux interoperability test remains
`scripts/kernel/verify-sim-wg-reboot.py`. Its historical failing baseline is in
[the reproduction record](tai64n-reboot.md). RTC-enabled and RTC-disabled
builds must be reported separately; a passing RTC test is not a pass for TT
as a whole. Physical RTC, rollback across reboot, and durable power-cut tests
remain outstanding.

### Results on 2026-09-23

NuttX `66b7403c8a` plus the uncommitted correction; apps `24b3f311` unchanged.
The isolated `wg-codex-locking` container was used, not the existing `wgdev`.

- Host C allocator test with UBSan: PASS, including 4,000 concurrent calls.
- RTC-enabled sim build: exit 0. Changed C/header files: checkpatch PASS.
- Same-key reboot with Linux peer unchanged: PASS, one initiation and one
  response, response at 0.695 seconds after second process launch, bound
  `wg0` ping success, no Linux-originated initiation.
- Before reboot: `400000006ab339ad1ad27480`; after reboot:
  `400000006ab339c326be3680`, strictly greater.
- T1 runtime configuration/save/down/restore: PASS.
- TR replay/endpoint and cookie-flood checks: PASS.
- RTC-disabled negative control: REPRODUCED (diagnostic mode, not a fix PASS).
  Six initiations had no response in 30 seconds, the bound ping failed, and
  catch-up recovery occurred at 75.299 seconds. The report was collected on
  September 27 from the completed September 23 run. #14 therefore stays open.
- CMake, KERNEL/PROTECTED, NuttX SMP, and physical boards: not rerun for this
  timestamp increment. Host pthread stress is not an SMP kernel test.

Local artifacts: `output/tai64n-realtime-validation/rtc-reboot/report.json`
and redacted boot logs, `rtc.config`, `rtc-build.log`, `runtime-summary.log`,
and `replay-summary.log`. The build still has the existing vendored userspace
X25519 warnings; this is not a warning-free build claim.
RTC-disabled artifacts are in `no-rtc-reboot/report.json`, `no-rtc.config`,
and `no-rtc-build.log` under the same validation directory.
