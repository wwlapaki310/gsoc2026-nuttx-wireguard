# Hardware resource measurement and lifecycle soak — Spresense, 2026-09-28

The sim soak (T7) returned numbers that were suspiciously clean: heap in use and
the live allocation count back to their boot values exactly, the IOB pool whole,
the `wg_rx` stack high-water flat at 2536 bytes. That is the right answer, but
the sim's allocator is the host's and its frames are 64-bit, so **none of the
heap figures transfer to a board and the stack figure transfers only as an upper
bound.** This is the same three metrics on real hardware.

Board: Spresense (Cortex-M4F), Wi-Fi through GS2200M over **usrsock**, tunnelling
to the official Windows WireGuard client over home Wi-Fi. Image built by
`scripts/kernel/build-spresense-kernel.sh --measure`, which adds
`CONFIG_STACK_COLORATION` and procfs so `ps` reports per-task high-water marks
and `/proc/iobinfo` is readable. That flag is off by default: painting stacks
costs time at every task start, which a demo image should not pay.

```text
image:  spresense-measure.spk (445856 bytes)
sha256: a7adca0cac4f3d8868a9e0b662c4709d371c14920dbba0b64a56f69569824a5e
config: e2f8652ece24cf7acc5c657a72182b1ef48af303fee303a4fd0a6e06aa2b20c8
nuttx:  cec617dfbce984bc0bf242922c3c4d306238c4de (nuttx-13.0.1)
apps:   be1ae4e896aa0739e00efdc3478f0d01175b82dc (nuttx-13.0.1)
```

Both runs are driven over the serial console by scripts that **read until the
prompt returns** rather than waiting a fixed time. That matters: a fixed wait
truncated `ps` and `free` often enough on the first attempt to make the numbers
unusable, and a truncated sample parses as "no data", which is easy to misread
as a passing check.

## Load measurement

`scripts/kernel/verify-spresense-resources.py COM6 120`. The board is reset and
brings itself up headless — Wi-Fi, saved configuration, `wg up` — so nothing is
typed until the tunnel already exists. Then three ping senders run from the host
for 120 s with 1000, 1000 and 200-byte payloads, so the driver's buffers see
more than the keepalive-sized traffic the other hardware tests produce.

**375 of 375 pings answered**, and `wg show` reported **296336 B received,
296320 B sent**.

| sample | heap used | allocs | iob free | `wg_rx` used |
|---|---|---|---|---|
| after bring-up, idle | 43760 | 112 | 8/8 | 1472 |
| under load | 43760 | 112 | 8/8 | 1472 |
| after the load stopped | 43760 | 112 | 8/8 | 1472 |

Per-task stack high-water, from `ps`:

| task | stack | used | filled |
|---|---|---|---|
| `Idle_Task` | 1000 | 548 | 54.8 % |
| `cxd56_pm_task` | 976 | 492 | 50.4 % |
| `hpwork` | 1976 | 352 | 17.8 % |
| `lpwork` | 1976 | 760 | 38.4 % |
| `spresense_main` | 3024 | 1696 | 56.0 % |
| **`wg_rx`** | **6096** | **1472** | **24.1 %** |

`nwait` and the throttle count were 0 in every sample.

**The number that matters for the submission** is `wg_rx`: **1472 bytes used of
the 6096 the configuration gives it**, on a Cortex-M4F, unchanged across idle,
load and rest. `CONFIG_NET_WIREGUARD_RXSTACKSIZE` defaults to 6144, and that
default is now backed by a measurement on real hardware rather than a guess. It
is also consistent with the sim's 2536 bytes being an upper bound: 64-bit frames
are larger, as expected.

## Lifecycle soak

`scripts/kernel/verify-spresense-lifecycle.py COM6 40`. T7 did 100 down/up
cycles in the sim, where the UDP socket is the host's. Here each cycle closes and
reopens a socket that belongs to the GS2200M module through usrsock, and each
`wg up` has to dial out and complete a fresh handshake over real Wi-Fi.

**40 cycles.** Traffic was checked every tenth cycle and came back every time
(4/4). No assertion, panic or unhandled exception was printed.

| | before | after 40 cycles |
|---|---|---|
| heap used | 43760 | 43760 |
| live allocations | 112 | 112 |
| IOB free | 8/8 | 8/8 |
| IOB waiting | 0 | 0 |
| `wg_rx` high-water | 1472 | 1472 |

Every figure is identical, which is the same result the sim gave and the reason
to trust it: a socket, buffer or queue entry leaked once per cycle would show up
forty times over.

## Also observed

The clock-unset warning (#14) fires on this board at every boot, as it should —
Spresense here has no set RTC, so `CLOCK_REALTIME` starts below the floor derived
from `CONFIG_START_YEAR`:

```text
nsh> [   11.610000] wireguard: the realtime clock looks unset; handshakes will be
refused by a peer that already saw a later timestamp for this key, until the
clock passes it. Set the time (RTC or SNTP) before bringing wg0 up.
```

The tunnel still comes up, which is the intended behaviour: this peer already
holds the key, and the warning is about what happens to a reconnect after a
reboot. It is the honest face of #14 remaining open.

## What this does not show

- **One board, one architecture.** ESP32-S3 was not measured, and its Wi-Fi is
  native rather than usrsock, so its numbers would differ.
- **Minutes, not days.** 120 s of load and 40 lifecycle cycles. The IOB pool
  never went below full, so throttling and `nwait` behaviour are still untested
  on hardware, as they are in the sim.
- The load is ICMP echo through the tunnel from a single host. It is not
  throughput testing, and it does not produce concurrent senders inside the
  board.
- Rekeys are not forced here: at this traffic level the session simply did not
  reach its rekey age often, so the 151 forced rekeys remain a sim result.
- `CONFIG_STACK_COLORATION` reports the deepest use **observed**, not the worst
  case. A path not taken in these runs is not counted.
