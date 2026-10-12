# BUILD_PROTECTED intermittent fault: root cause — rv-virt:pnsh64, 2026-10-12

Follow-up to [protected-runtime-2026-09-28.md](protected-runtime-2026-09-28.md) and
`submission-checklist.md` §2.3, which recorded that repeated
`verify-pnsh-wg.sh` runs fault intermittently while `wg` configures `wg0`.

## Result

**The `wg` task overflows its 4096-byte stack.** In this configuration a system
call runs on the calling task's *user* stack, so that stack holds, at the same
time:

- the `wg` command's own frames,
- the kernel ioctl path into the driver, which does an X25519 scalar
  multiplication for `SIOCSWGIF` with a private key (`wg_device_init`) and for
  `SIOCSWGPEER` (`wg_peer_init`),
- the 528-byte trap frame for the syscall, and another 528-byte frame when an
  interrupt arrives while the X25519 code is at its deepest point.

When the interrupt lands at that point, the frame is written across the
`struct tls_info_s` that `CONFIG_TLS_ALIGNED` keeps at the lowest address of the
stack. Its first word, `tl_task`, is overwritten with a curve25519 field limb.
The next `task_get_info()` returns that value, and the first lock taken through
it faults. That is the "garbage address" in `nxsem_wait`.

It is in our code's resource budget (the default `CONFIG_SYSTEM_WG_STACKSIZE`
does not cover the driver's ioctl path in a protected build). It is not a
memory-safety bug in the driver or the command, not an ioctl struct lifetime
problem, and not a race with `wg` exiting. NuttX behaves as documented: the
`ARCH_KERNEL_STACK` help says that without it "kernel system calls will simply
use the caller's user stack", and that option only exists with
`ARCH_ADDRENV`, so it is unavailable on this MPU build.

With `CONFIG_SYSTEM_WG_STACKSIZE=6144` the same loop passes 20 of 20 runs (unmodified: 7 of 10 and 9 of 10 fail).

## Setup

| | |
|---|---|
| nuttx | `pr-latest/doc-system-wg` = `237b6e538e` (on upstream `4bc1ab2fd7`) |
| apps | `pr-latest/system-wg` = `fc640afc4` |
| build | `scripts/kernel/build-pnsh-wg.sh` (unchanged) |
| run | `scripts/kernel/verify-pnsh-wg.sh`, with `cd` pointed at a saved image directory so a rebuild cannot change the image under a running loop |
| container | `nuttx-wg-ci:latest` + `curl` (OpenAMP download) + `gdb-multiarch` |
| QEMU | 8.2.2, `-M virt,aclint=on -cpu rv64 -smp 1 -bios none` |

Relevant effective options: `BUILD_PROTECTED=y`, `TLS_ALIGNED=y`,
`TLS_LOG2_MAXSTACK=13`, `STACK_COLORATION=y`, `ARCH_INTERRUPTSTACK=2048`,
`SYSTEM_WG_STACKSIZE=4096`, `ARCH_KERNEL_STACK` not set (not selectable).

## Reproduction

Unmodified series, two loops of 10 consecutive runs: **9/10 fail** (first loop) and **7/10 fail** (second loop, from a saved copy of the same image). The first loop ran before the image was copied aside; no run in it shows output from the debug patch, so all used the unmodified image.
Every failing run faults right after one of the two commands whose ioctl does
X25519:

| command that faulted | runs | first exception |
|---|---|---|
| `wg set private-key …` | 10 (6 + 4) | kernel, `getumask` (`lib_umask.c:87`) called from `nx_vopen` while saving the config file. `A4`/`A5` = `0x80204000`, the stack base |
| `wg set peer …` | 6 (3 + 3) | user, `nxsem_wait` (`sem_wait.c:141`) ← `lib_flushall` (`lib_libflushall.c:121`) ← `fflush` ← `exit` ← `wg_main` |

The `MTVAL`s are all below 2^26 (`0x3f35812`, `0x206d4d1`, `0x2380832`,
`0x1640b95`, …), i.e. they look like 25/26-bit curve25519 limbs. The secondary
fault during the dump (`nxtask_argvstr`, `task_argvstr.c:96`, via
`nxtask_exithook`) reads the argument area next to the same TLS block.
`wg genkey`, `wg set listen-port` and `wg set address` never faulted.

The earlier record (2/8, 1/9) used a different image; the rate depends on how
often an interrupt meets the deepest X25519 frame, so it is not expected to be
stable across images.

## Evidence

### 1. What overwrites `tl_task`

A gdb hardware watchpoint on the stack base word (`0x80204000`, the same
allocation for every `wg` invocation in this boot) logged every write. Before
the failing command, the only writers are stack colouring
(`up_create_stack`), `tls_init_info` storing `tl_task` (`0x802011e0`), and
`up_release_stack` at exit. During `wg set peer` the next writer was:

```text
=== WP hit pc=0x80000120 sp=0x80203f80 new=929de4f
#0  exception_common () at common/riscv_exception_common.S:139    (save_ctx)
#1  fe_mul_tll (...) at curve25519.c:669
#2  curve25519 (...) at curve25519.c:1169
```

That is the interrupt entry saving a 528-byte context at `sp=0x80203f80`: it
covers `0x80203f80..0x80204190`, which is 128 bytes *below* the stack
allocation plus the whole TLS block, while the kernel's `curve25519()` was
running on `wg`'s stack inside the ioctl.

### 2. How much of the stack is used

A debug-only patch (not part of any fix) printed, after each driver ioctl and
at each task exit, `up_check_tcbstack()` and `tl_task`:

```text
4096-byte stack:
DBG ioctl 746 by wg: ... base=0x802040a0 alloc=0x80204000 size=3936 used=3936 tl_task=0x802011e8
DBG ioctl 746 by wg: ... base=0x802040a0 alloc=0x80204000 size=3936 used=3704 tl_task=0x4f9add2
DBG ioctl 746 by wg: ... base=0x802040a0 alloc=0x80204000 size=3936 used=3936 tl_task=0x3fffffe
```

(`0x746` = `SIOCSWGIF`, here carrying the private key.) With 4096 the whole
usable stack is consumed and `tl_task` becomes a limb-sized value; 4 of 4
runs of this image faulted.

The same patch with an 8192-byte stack, 6 runs, all PASS. Peak `used` per
command (the usable size is the allocation minus TLS and argv, 0x50–0x110):

| command | peak used |
|---|---|
| `wg genkey` | 3344 |
| `wg set private-key` (`SIOCSWGIF`) | 4064 |
| `wg set listen-port` / `address` | 3840 / 3368 |
| `wg set peer` (`SIOCSWGPEER`) | **4240** |
| `wg up` / `down` / `show` | 3984 / 3984 / 3368 |

`wg set peer` with 4096 has 3824 bytes usable, so the peak exceeds it by about
400 bytes.

### 3. Where the bytes go

Prologue frame sizes from `objdump` of this build, along the path gdb
reported at `curve25519` entry:

| side | frames | bytes |
|---|---|---|
| user | `wg_main` (sub-commands are inlined into it) 1568, `wg_set_if` 32, `wg_ioctl` 16, `ioctl` 64 | ~1680 |
| trap | `exception_common` (syscall) | 528 |
| kernel | `dispatch_syscall` 32, `STUB_ioctl` 16, `ioctl` 112, `file_vioctl` 96, `psock_ioctl` 80, `psock_vioctl` 64, `wg_ioctl` 176, `wg_device_init`/`wg_peer_init` 48, `wg_generate_public_key` 32, `wg_x25519*` 16, `curve25519` 672, `fe_mul_impl` 176 | ~1520 |
| interrupt | `exception_common` (the ISR itself runs on the 2 KB interrupt stack) | 528 |
| total | | **~4260** |

This matches the measured 4240. The fault is intermittent because the last
528 bytes only appear when an interrupt (timer, virtio) is taken while the
X25519 code is near its deepest frame.

## What was ruled out

| candidate | finding |
|---|---|
| user object freed or overwritten by the command | the bad pointer is `tl_task` in the TLS block, written only by the trap frame shown above |
| kernel dereferencing a user pointer from the ioctl struct | the requests are flat; the writer is `exception_common`, not driver code |
| race between `wg` exit and a kernel callback | the corruption is in place before `exit()`; with a larger stack no run faults |
| stock-NuttX PROTECTED bug | the behaviour is the documented one (syscalls on the user stack without `ARCH_KERNEL_STACK`). A separate loop with a trivial program on stock `pnsh64` was not run; the watchpoint and the measurements already attribute every write |

## Fix

Patch (apps). Folded into the PR-A commit on 2026-10-12: nuttx-apps `pr-latest/system-wg` is now `0ff6a0984` + `3a6d641d9`; the previous tip is `bak/pr-latest-20261012/system-wg`:
`0001-system-wg-protected-stack.patch` in the scratchpad `protfault/`
directory. It makes `CONFIG_SYSTEM_WG_STACKSIZE` default to 6144 when
`BUILD_PROTECTED`, keeps 4096 otherwise, and says why in the help text.
6144 leaves about 1.6 KB above the measured peak plus TLS/argv, and is below
the 8192 limit that `TLS_LOG2_MAXSTACK=13` imposes on a `TLS_ALIGNED` stack.

Result with the patch (`build-pnsh-wg.sh` picks up the new default, no other
change): **20/20 PASS**, each run reaching `PASS: BUILD_PROTECTED runtime verified`, with no `EXCEPTION`, `PANIC` or `_assert` anywhere in the 20 consoles. Same loop, unpatched image, run right after: 7/10 fail.

## Notes

- The same arithmetic applies to any build that runs the ioctl on the caller's
  stack. In a FLAT RISC-V build there is no syscall trap frame, so the estimate
  is about 3.7 KB of 4096; that was not measured.
- In BUILD_KERNEL the path runs on the per-process kernel stack. The kernel
  part plus two trap frames is about 2.6 KB here; `rv-virt:knetnsh64` sets
  `ARCH_KERNEL_STACKSIZE=3072`, while the Kconfig default is 1568. Not
  measured on that build.
- Not changed: the driver still does X25519 inside the ioctl. Moving it off
  the caller's stack, or shrinking `wg_main`'s 1568-byte frame, are
  alternatives to a larger stack; neither was tried.
- Logs: `C:\Users\0000400096\workspaces\dbg-prot\logs\` (`base`, `base2`,
  `dbg1`, `dbg2`, `fix`, `gdb1.log`, `gdb2.log`; a copy is in the scratchpad `protfault/logs/`). Keys in them are throwaway
  keys generated inside the container per run.
