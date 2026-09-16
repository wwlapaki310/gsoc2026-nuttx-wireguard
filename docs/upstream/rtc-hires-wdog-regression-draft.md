# CONFIG_RTC_HIRES + late RTC init: issue draft for apache/nuttx

Found while building this repository's `spresense:wifi`-based image against
NuttX `master` (bda22516, 2026-09-17). The same configuration boots on
NuttX 12.7.0. Nothing here is specific to WireGuard; the board hangs before
`nsh_main()` with the stock `spresense:wifi` defconfig. Status: **draft, not
filed** (2026-09-17).

Suggested title: `sched/clock: RTC_HIRES boards that enable the RTC from a
watchdog never boot (clock_systime_ticks() is 0 until the RTC is up)`

---

## Symptom

`spresense:wifi` on `master` prints nothing on the console and never reaches
NSH. No assertion, no crash dump: the CPU is idle. The same defconfig on
`nuttx-12.7.0` boots normally.

Bisected by configuration first (GS2200M, storage, LCD, audio, USB, the
extension board, ELF loader, stack sizes and `STANDARD_SERIAL` were all
ruled out); the one option that turns the hang on and off is
`CONFIG_RTC_HIRES`. `spresense:nsh` (which does not select it) boots.

Trace markers (`_err()` on the raw console) in
`boards/arm/cxd56xx/spresense/src/cxd56_bringup.c` show the last thing to
run is `board_clock_initialize()` inside `board_power_setup()`; the next
statement is never reached.

## What happens

Three pieces, each fine on its own:

1. `sched/sched/sched_processtick.c` now runs the watchdog list with

   ```c
   wd_timer(clock_systime_ticks());
   ```

   On 12.7.0 the argument was the scheduler's own tick counter. With
   `CONFIG_RTC_HIRES=y`, `clock_systime_ticks()` is derived from
   `clock_systime_timespec()`, which is RTC time.

2. `sched/clock/clock_systime_timespec.c` (RTC_HIRES branch) returns
   `{0, 0}` until `g_rtc_enabled` is set:

   ```c
   if (g_rtc_enabled)
     {
       up_rtc_gettime(ts);
       ...
     }
   else
     {
       ts->tv_sec = 0;
       ts->tv_nsec = 0;
     }
   ```

   So while the RTC is not yet enabled, every timer tick calls
   `wd_timer(0)` and no watchdog ever expires.

3. `arch/arm/src/cxd56xx/cxd56_rtc.c` with `CONFIG_CXD56_RTC_LATEINIT`
   (selected by the Spresense configs) cannot enable the RTC synchronously:
   it waits for the external RTC to synchronise by re-arming a watchdog
   (200 ms, up to 15 retries) and sets `g_rtc_enabled` from the callback.

That is a cycle. The watchdog that would enable the RTC cannot fire until
the RTC is enabled. Independently, `board_power_control()` on cxd56 calls
`nxsched_usleep(1)` on the way out of `board_clock_initialize()`; that sleep
is an absolute-tick wait against the same frozen clock, so the boot thread
parks there forever. That is the spot the trace markers point at.

`cxd56_rtc_initialize()` is written on the assumption that
`clock_systime_timespec()` reports the time elapsed *before* the RTC came
up (it folds that into the base-time offset), so returning the scheduler's
tick counter until then is what the driver already expects, not a change of
contract.

## Proposed fix (sched)

Return the scheduler tick counter instead of zero while the RTC is not yet
enabled. Applied to `master` in this repository's Dockerfile as a build-time
patch (`sched/clock/clock_systime_timespec.c`):

```c
  else
    {
      /* RTC not yet enabled: fall back to the scheduler tick counter so
       * that watchdogs (driven by clock_systime_ticks()) keep expiring
       * and RTC late-initialisation can complete.
       */

      clock_ticks2time(ts, clock_get_sched_ticks());
    }
```

With this one change `spresense:wifi` on `master` boots, `/etc/init.d/rcS`
runs, the GS2200M associates, and the WireGuard handshake with the Windows
client completes in ~10 s, followed by telnet and HTTP through the tunnel.
Verified on hardware (Spresense main board + iS110B v1.0C).

An alternative is for `sched_processtick.c` to keep feeding `wd_timer()`
with `clock_get_sched_ticks()` regardless of `CONFIG_RTC_HIRES`; that is a
larger behavioural change for boards whose RTC is available from reset, so
the clock-side fallback looks like the smaller patch.

## Related, not the cause: `up_rtc_settime()` and `g_rtc_lock`

`master`'s `cxd56_rtc.c` takes `g_rtc_lock` in `up_rtc_settime()` and then
calls `cxd56_rtc_count()`, which takes the same lock. Without
`CONFIG_SPINLOCK` this degrades to nested `irqsave` and is harmless (which
is why it was initially suspected and then cleared); with `CONFIG_SPINLOCK`
or SMP it would be a recursive spinlock. The same commit added
`cxd56_rtc_count_nolock()`, so the fix is to use that from the locked
region:

```c
-  g_rtc_save->offset = count - cxd56_rtc_count();
+  g_rtc_save->offset = count - cxd56_rtc_count_nolock();
```

Also applied in the Dockerfile. Worth a one-line PR on its own.

## Before filing

- `git log -S'wd_timer(clock_systime_ticks())' -- sched/sched/sched_processtick.c`
  to cite the commit that changed the argument, and check whether other
  `RTC_HIRES` + late-init boards (anything calling `clock_systime_timespec()`
  from a watchdog before `g_rtc_enabled`) show the same hang.
- Confirm whether the CI for `spresense:wifi` still passes on `master`;
  a build-only CI would not catch this, which is presumably why it landed.
- The Dockerfile applies both patches only when the pre-patch pattern is
  present, so the build on `nuttx-12.7.0` is unaffected.
