# BUILD_PROTECTED runtime — rv-virt:pnsh64, 2026-09-28

Until now PROTECTED was only ever a **build**: `mr-canhubk3:knsh` links the
driver into the kernel and `apps/system/wg` into `nuttx_user.elf`, but that
board has no emulator, so nothing had ever run. The thing a build cannot show is
whether the ioctl ABI survives the boundary. Every request is a fixed struct
with no embedded pointers precisely so the kernel never dereferences a user
address — and the way that assumption fails is an **MPU exception at the first
ioctl**, not a link error.

`rv-virt:pnsh64` is a first-class NuttX PROTECTED config for a target QEMU runs,
and rv-virt's virtio-net is the same known-good device the BUILD_KERNEL runtime
proof uses. So it is the one path to PROTECTED runtime without new hardware.

Build: `scripts/kernel/build-pnsh-wg.sh`. Run: `scripts/kernel/verify-pnsh-wg.sh`.

```text
Host  tapwg  10.0.0.1/24  + Linux wgtest0 (listen 51821), tunnel 10.10.0.1
Guest eth0   10.0.0.2/24  + NuttX wg0     (listen 51820), tunnel 10.10.0.2
```

## What had to be configured, and what did not

`pnsh64` ships with no networking, so the stack, virtio-net, the driver and the
command are switched on. Three things were less obvious:

| Needed | Why |
|---|---|
| `SCHED_HPWORK`, `SCHED_LPWORK` | `netdev_upperhalf` and the TCP timers run on the work queues; without them the link fails on `work_queue`/`work_cancel` |
| `DEV_SIMPLE_ADDRENV` | virtio arrives through OpenAMP, whose libmetal calls `up_addrenv_va_to_pa` **unconditionally**. That symbol only exists with `CONFIG_ARCH_ADDRENV`, i.e. an MMU build. This is an MPU build, so the generic implementation supplies it — and with no translation table registered it is the identity map, which is exactly right when VA == PA |
| `NET_LL_GUARDSIZE=32` | virtio-net prepends its own header; a smaller guard fails a static assertion in the driver |
| NSH as the init task | `pnsh64` boots through `nxinit`, whose `init.rc` starts a console service by exec'ing `sh` along `CONFIG_PATH_INITIAL` (`/system/bin`). Nothing in this configuration mounts binfs there, so the exec fails with ENOENT and the service restarts forever (`Error Starting service 'console': 2`). Starting NSH directly is what every other config on this board does, and nothing about the driver or the MPU split depends on which one brings up the shell |

**The memory split was left alone.** The protected layout divides RAM at
`CONFIG_NUTTX_USERSPACE`, stock `0x80040000`, giving 256 KB each side. Measured:

| | `.text` | of 256 KB |
|---|---|---|
| kernel (`nuttx`) | 209 832 B | 80 % |
| user (`nuttx_user`) | 21 576 B | 8 % |

So this is stock `pnsh64` plus networking, with no board file and no layout
change — worth stating, because "we had to move the memory map" would be a much
weaker result.

## Transcript

```text
ABCD
NuttShell (NSH)
nsh> ifconfig eth0 10.0.0.2
nsh> wg genkey
6KITt16pWOruSW5/9CD8Cj2SWvrWDVkOdqMpN90J22c=
nsh> wg set private-key 6KITt16pWOruSW5/9CD8Cj2SWvrWDVkOdqMpN90J22c=
wg: cannot write '/tmp/wg0.conf.tmp': No such file or directory
wg: the device now uses the new key, but it could not be saved to /tmp/wg0.conf; it will be lost on restart
nsh> wg set listen-port 51820
nsh> wg set address 10.10.0.2/24
nsh> wg set peer 77HCEuVxbXQEFv4ZzSU1rgMyl0HbIuWly7R7bh/tpzo= endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 25
nsh> wg up
wg0 is up (listen port 51820)
nsh> wireguard: the realtime clock looks unset; handshakes will be refused by a peer that already saw a later timestamp for this key, until the clock passes it. Set the time (RTC or SNTP) before bringing wg0 up.
nsh> wg show
interface: wg0
  public key: Seox4fz/aTK5RmdO9PMAbACAzUshSZNYdnhUpvbXeDM=
  listening port: 51820
peer: 77HCEuVxbXQEFv4ZzSU1rgMyl0HbIuWly7R7bh/tpzo=
  endpoint: 10.0.0.1:51821
  latest handshake: 4 seconds ago
  transfer: 464 B received, 448 B sent
  persistent keepalive: every 25 seconds
nsh> wg down
nsh> wg up
wg0 is up (listen port 51820)
```

(The keys are throwaway, generated inside the container for this run.)

## Checks, all passing

| | |
|---|---|
| boots with the MPU split and the user blob loaded | `NuttShell (NSH)` |
| offline key generation works in user space | `wg genkey` returned a key |
| the configuration ioctls cross the boundary | no exception, no assertion, no panic in the console |
| a real tunnel to Linux kernel WireGuard | host ping to `10.10.0.2` 3/3 after the handshake |
| the Linux peer agrees | `wg show wgtest0` records a handshake |
| the read direction of the ABI works | the guest's `wg show` reports the handshake **and** `464 B received, 448 B sent` — the kernel filled a struct the user side supplied |
| `ifdown` with a user task waiting on the ioctl | `wg down` then `wg up`, no fault, tunnel returns |

The console is checked for `EXCEPTION`, `riscv_exception`, `Data access`,
`Instruction access`, `PANIC`, `Assertion failed` and `_assert:` at three points
during the run, not just at the end.

## Two things this run confirmed by accident

Both are worth keeping, because neither was what the run was for.

**The configuration-save fix (#17) doing its job on a real target.** `pnsh64` has
no writable `/tmp`, so `wg set private-key` could not persist the key. It said
so, and it said what the consequence is:

```text
wg: cannot write '/tmp/wg0.conf.tmp': No such file or directory
wg: the device now uses the new key, but it could not be saved to /tmp/wg0.conf; it will be lost on restart
```

Before the fix this path returned void and the command reported success. This is
the first time that failure has been observed on a target rather than injected.

**The clock-unset warning (#14) firing on a real boot.** `pnsh64` has no RTC and
`CONFIG_START_YEAR=2021`, so the seeded clock is below the derived floor and the
driver warns once, naming the consequence. The tunnel still came up, which is the
intended behaviour: this peer had never seen the key before, so it accepts a low
timestamp. What the warning is about is the *next* boot.

## What this does not show

- QEMU with an MPU model, not silicon. It proves the ABI does not need to
  dereference user memory and that the split does not break the driver; it does
  not prove a particular board's MPU region configuration.
- `mr-canhubk3:knsh` remains a build only. This is a different architecture
  (RISC-V, not Cortex-M) with a different MPU.
- The fault cases tested elsewhere — a held send on a real backend, IOB
  exhaustion, stop timeouts — were not repeated here.
- One run of a down/up cycle, not the 100 that T7 does in the sim.
