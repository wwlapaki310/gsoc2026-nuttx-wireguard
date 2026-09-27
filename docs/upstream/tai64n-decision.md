# Handshake timestamps without a trustworthy clock: options and the call

Companion to [tai64n-design.md](tai64n-design.md) (the implementation record)
and [tai64n-reboot.md](tai64n-reboot.md) (the original failure). This page is
the **comparison and the decision**, written to be defended rather than asked.

## The constraint

A WireGuard responder keeps, per peer, the greatest handshake timestamp it has
accepted, and drops any initiation carrying an equal or smaller one. That is
what makes a captured initiation unusable later. The cost is a real obligation
on the initiator: **every initiation must carry a value strictly greater than
anything that peer has already accepted for this key — across reboots.**

An embedded device is exactly where that is hard: it is usually the initiator
(it dials out to a server through NAT), and it is often the machine least
likely to have a battery-backed clock.

## What the failure actually looks like

The reproduction measured recovery at about 75 seconds, which reads as a minor
annoyance. It is not, and the number is misleading.

With a since-boot clock the device restarts at zero and has to climb back past
the value the peer remembers. That value is the timestamp of its **last
handshake in the previous session**, so:

> the outage after a reboot is as long as the previous session's uptime.

75 seconds was only because the previous session was short. A device that ran
for thirty days cannot reconnect for thirty days. Any option that relies on
"it catches up eventually" is therefore not an option at all.

## The options

| | Durable writes | Works with no RTC | First pairing | Complexity | Call |
|---|---|---|---|---|---|
| **A. Realtime + in-boot high-water** | none | no | yes | trivial | **adopted** |
| B. Persist every emitted timestamp | one per handshake | yes | yes | medium, plus wear | rejected |
| C. Durable range reservation | one per N handshakes | yes | yes | high | deferred, designed |
| D. Persisted boot counter | one per boot | yes | needs seeding | medium | rejected for v1 |
| E. Responder-only on clockless boards | none | n/a | yes | trivial | rejected |
| F. Let it catch up | none | no | yes | none | rejected outright |

**A — realtime plus an in-boot high-water mark.** Read `CLOCK_REALTIME`, and
never emit a value below the last one issued this boot, so a clock that repeats
or is stepped backwards cannot produce a duplicate. Costs nothing, fixes every
board that keeps time, and is what the Linux kernel and wireguard-go do. It
does not fix a board with no clock.

**B — write the last timestamp on every handshake.** Correct and simple to
reason about, and wrong in practice: rekeys happen every couple of minutes, so
this is a flash write on the handshake path, forever. Wear and added latency on
the one path that must stay responsive.

**C — reserve a range durably, spend it from RAM.** Reserve an upper bound,
allocate below it in memory, re-reserve when it runs low; a power cut loses the
unspent remainder, which is harmless because skipping forward is allowed. This
is the right long-term answer and it is written up in `tai64n-design.md`. It is
deferred because it needs something NuttX does not offer generically: a storage
backend that will *state* its ordering and durability guarantees. Locally,
`hostfs_sync()` calls the void `host_sync()` and returns OK, so a successful
write in sim proves nothing about power loss. Shipping a reservation scheme on
top of a backend that cannot promise the bound is durable would be worse than
not shipping it, because it would look solved.

**D — persist a counter, bump it once per boot.** Cheap on writes and
genuinely monotonic. Rejected for now for two reasons. It needs the same
durability contract as C, so it does not dodge the hard part. And the value it
produces is no longer a time: if the peer previously accepted a real timestamp
from this key, a counter-derived value has to be seeded above it or the device
never reconnects — which drags in provisioning and a trust model for that seed.

**E — declare clockless boards responder-only.** Honest and free, but it
removes the common deployment: the device behind NAT that dials out. A VPN that
only works when the far side initiates is not the feature people want.

**F — do nothing and let the clock catch up.** Ruled out by the section above:
the wait is unbounded in any way that matters.

## The call

**Adopt A, and make the case it does not cover loud instead of silent.**

Concretely, what the driver does now:

1. Take the timestamp from `CLOCK_REALTIME`, not from uptime.
2. Keep an in-memory high-water mark shared across interfaces, so successful
   allocations strictly increase within a boot even across `down`/`up`, peer
   deletion and key changes.
3. Never fall back to uptime. On an invalid clock, a lock failure or
   exhaustion, fail and let `wg_create_init()` abort before it touches
   handshake state — a wrong timestamp is worse than no handshake.
4. **Warn once when the clock looks like it was never set.** Without an RTC,
   NuttX seeds `CLOCK_REALTIME` from `CONFIG_START_YEAR`, so a realtime still
   below the following year is almost certainly unset. The driver still issues
   the timestamp — a peer that has never seen this key accepts it, and
   refusing would break first-time pairing — but it says, once, that
   reconnection after a reboot will fail until the clock passes what the peer
   remembers.
5. State the requirement in Kconfig help and the driver documentation:
   the platform must establish time, from an RTC or SNTP, before `wg up`.

Point 4 is the part worth arguing for. The failure it addresses is not a crash
or an error code; it is a tunnel that simply never comes back, on a device that
is probably not on a desk. Turning that into one line on the console is a small
change with a large effect on how long it takes someone to work out why.

## What this deliberately does not claim

- It does not fix clockless boards. Those still cannot reconnect with the same
  key after a reboot, and #14 stays open for that.
- The heuristic detects "never set". It cannot tell a plausible-but-wrong date
  from a correct one and does not try.
- A large forward jump followed by a rollback can leave the in-memory mark
  ahead of realtime; a reboot loses that protection.
- Using one private key on two machines is not made safe by a local allocator.

## Why this is the right shape for review

The mainstream implementations — Linux, wireguard-go — take the timestamp from
the realtime clock and assume the platform provides one. Matching that keeps
the driver's contract familiar, and puts the platform requirement where it
belongs: in the platform. What is added here is the thing a desktop
implementation never needed, because a desktop always has a clock — telling the
operator, on the device, that theirs does not.
