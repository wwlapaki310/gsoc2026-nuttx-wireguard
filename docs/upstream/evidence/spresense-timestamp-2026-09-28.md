# The timestamp problem (#14) measured on hardware — Spresense, 2026-09-28

WireGuard's responder drops any handshake timestamp at or below the greatest it
has already accepted for that key, so an initiator has to beat its own past
**across a reboot**. A board without a retained clock cannot. That is
[#14](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/14), and
until now it rested on the protocol plus a simulator reproduction. The
[decision record](../tai64n-decision.md) explains why the adopted design is
realtime plus an in-boot high-water mark, and why the driver warns instead of
refusing.

This is the same thing on real hardware, and it also answers the other half:
that where a clock *does* exist, the design works.

Run with `scripts/kernel/verify-spresense-timestamp.py COM6` against the
measurement image ([build record](spresense-resources-2026-09-28.md)).

## Why a ping test is not enough

The first attempt looked like a pass and was worthless. Reset the board, ping it,
and the tunnel answers in 16 s — because **the ping makes the peer initiate**,
and a responder needs no timestamp of its own. The board can be completely unable
to dial out and that test will still succeed.

So the real measurement has to send the board nothing at all and watch its own
view of the session.

## The pair

Two arms differing in exactly one thing. Same board, same peer, same image, and
nothing sent to the board in either.

| | arm A: clock as it boots | arm B: clock set from the host |
|---|---|---|
| clock after boot | `Thu, Jan 01 00:00:38 1970` | `Mon, Sep 28 06:56:16 2026` |
| driver warned about the clock | **yes** | **no** |
| handshake, nothing sent to the board | **none after 75 s** | **completed in 4.1 s** |

```text
=== arm A: clock as it boots ===
   clock: Thu, Jan 01 00:00:38 1970
   driver warned about the clock: True
   watching for 75s, sending the board nothing
       0.1s  (never) | rx 0 tx 0

=== arm B: same, with the clock set from this host ===
   clock: Mon, Sep 28 06:56:16 2026 (set to Sep 28 06:56:16 2026 UTC)
   driver warned about the clock: False
   watching for 75s, sending the board nothing
       0.1s  (never) | rx 0 tx 0
       4.1s  3 seconds ago | rx 0 tx 16
```

The only difference is the clock, so the refused timestamp is the cause. Two
things follow, and they are both worth stating:

- **#14's open case is real on hardware, not just in a simulator.** A board that
  must dial out and has no retained clock does not come back. The wait is not a
  fixed 75 seconds either — it is as long as the previous session ran, because
  the peer remembers the last handshake of that session.
- **The design is right wherever a clock exists.** Given the time, the board
  dials out and the peer accepts it in about four seconds, and the warning goes
  quiet. Nothing in the driver has to be special-cased for that.

## Supporting observations

- **The clock boots at the epoch, not `CONFIG_START_YEAR`.** The unset-clock
  heuristic in `wg_tai64n.c` compares against the year after
  `CONFIG_START_YEAR`; on this board realtime starts at 1970, far below it, so
  the check fires with a very wide margin rather than marginally.
- **The RTC does not retain the time across a reset here.** Setting the clock,
  resetting, and reading it back gives 1970 again. So this board is squarely in
  the uncovered case, which is why it is a useful place to measure it.
- **The warning is not repeated once the clock is set** — it is issued once per
  boot at most, and arm B confirms the condition is re-evaluated rather than
  latched from the first `wg up`.

## What this does not show

- **One peer, and a cooperative one.** The Windows client here answers an
  initiation. A peer behind NAT that only ever responds would fail differently.
- **Reset, not power-off.** A DTR reset is enough to lose the clock on this
  board, so the distinction did not matter for arm A, but a board whose RTC
  survives resets and not power cuts would behave differently between the two.
- **No fix is demonstrated, because none is claimed.** Durable range reservation
  is designed in [tai64n-design.md](../tai64n-design.md) and deliberately not
  implemented: NuttX offers no storage durability contract to build it on
  (`hostfs_sync()` calls a void `host_sync()` and returns OK, so a successful
  write in the simulator says nothing about power loss). **#14 stays open.**
- Setting the clock by hand is not a solution for a deployed device; SNTP or a
  battery-backed RTC is. This shows the mechanism works once the time is right,
  not that acquiring the time is solved.
