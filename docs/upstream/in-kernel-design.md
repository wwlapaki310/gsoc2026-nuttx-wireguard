# In-kernel WireGuard — implementation design

What the in-kernel driver actually does, read from the code (fork `net-wireguard`
`0c1bf89de2` plus the device-lock/queued-output working-tree fix), so a reviewer can judge
concurrency, ownership, and cleanup without reading every line. This describes the
**implementation**; planned-but-unverified behaviour is called
out as such. Verified status per test: [verification-matrix.md](verification-matrix.md).

## Components

| File | Role |
|---|---|
| `drivers/net/wireguard/wireguard.c` | the `netdev_lowerhalf` device: socket, RX thread, timers, TX/RX, the five ioctls |
| `drivers/net/wireguard/wg_noise.c` | protocol core (handshake, key schedule, anti-replay, cookie), ported from wireguard-lwip |
| `drivers/net/wireguard/wg_crypto.c` | thin layer over NuttX `crypto/` (BLAKE2s, ChaCha20-Poly1305, Curve25519) |
| `include/nuttx/net/wireguard.h` | the flat, pointer-free ioctl ABI |
| `net/netdev/netdev_ioctl.c` | dispatches the `SIOC*WG*` commands to `dev->d_ioctl` (not registered in `net_ioctl_arglen()`) |

## Actors

Two contexts touch a device:

1. **The network stack (upper half).** Calls `wg_ifup` / `wg_ifdown` / `wg_transmit` /
   `wg_receive` / `wg_ioctl`. TX and generic up/down callbacks hold the device's recursive
   `d_lock`, not global `net_lock`. `wg_ioctl` explicitly takes the same device lock.
   WireGuard uses `NETDEV_RX_DIRECT`: its own RX thread notifies the upper half outside
   protocol processing, without an additional upper-half work item to cancel on down.
2. **The receive kernel thread** `wg_rx` (one per interface, priority
   `CONFIG_NET_WIREGUARD_RXPRIORITY`, stack `CONFIG_NET_WIREGUARD_RXSTACKSIZE`, default 6144).
   Created in `wg_ifup`, exits in `wg_ifdown`.

## Lifecycle / state transitions

```
register (drivers_initialize)      wg_ifup                       wg_ifdown
  netdev_lower_register(wgN)   ->   psock_socket + psock_bind ->  running=false; wait rxdone
  no socket, no thread              start wg_rx thread            psock_close; clear sessions;
  (upper half registered)           bifup=true, running=true      drain queues; bifup=false
```

- At **registration** the upper half is allocated, but there is no socket or RX thread.
  Those are created on up (matches the Spresense rcS order: the usrsock Wi-Fi daemon is up
  before `wg up`, so the UDP socket is opened while usrsock is alive).
- **`wg_ifup`** refuses without a private key (`-EINVAL`); opens the UDP socket bound to the
  listen port on `INADDR_ANY`; arms handshakes for peers that have a configured endpoint;
  then starts the `wg_rx` thread. Every failure path unwinds what it created (close the socket,
  clear `running`) before returning the errno.
- **`wg_ifdown`** clears `bifup`, drops carrier, sets `running=false`, and waits for the RX
  thread to actually exit before touching the socket or the semaphore. The thread checks
  `running` in both its outer loop and its inner drain loop and posts `rxdone` as its last act,
  and is explicitly woken on stop. A blocking backend may delay its exit;
  `wg_ifdown` (via `wg_rx_teardown`) retries the bounded wait
  and only then closes the socket, clears `rxpid`, destroys `rxdone`, then `wg_down_finish` wipes
  every peer's session (`wg_peer_clear_sessions`), resets endpoints, and drains the RX queue. The
  device lock is released while waiting via `net_sem_timedwait2`; `reaping` excludes a
  concurrent second waiter. Pending encrypted output is freed only after the worker exits.
  If the wait times out, it does **not** close the socket from under a live thread:
  it returns an error, so the netdev layer keeps `IFF_UP` set and the interface is left
  *stopping*. Recovery is a **repeated `ifdown`** — while `IFF_UP` is set, `netdev_ifdown` calls
  `wg_ifdown` again, which reaps the now-exited thread and finishes teardown, clearing `IFF_UP`;
  a subsequent `ifup` then comes up cleanly. The WireGuard up ioctl returns `-EBUSY` while
  stopping; generic `netdev_ifup` would otherwise skip the callback while `IFF_UP` is set.
  A test build option `CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL` forces this path so the
  recovery can be exercised deterministically (`verify-sim-wg-stop-recovery.sh`).

## Data path

- **TX** (`wg_transmit`, upper half): under `d_lock`, copy the inner IP packet to
  `plainbuf`, route to a peer, encrypt into `cryptbuf`, and copy the resulting UDP datagram
  and endpoint into an owned queue entry. Missing sessions trigger a rate-limited handshake.
  Queue pressure is distinct from missing sessions and does not request another handshake.
- **Socket output** (`wg_flush_tx`, RX thread only): remove a queue entry under `d_lock`,
  release the lock, then call `psock_sendto(MSG_DONTWAIT)`. The entry contains no peer or
  keypair pointers. The worker frees it when send returns. The limit of four entries includes
  any in-flight entry; full queues/allocation failures drop rather than wait.
- **RX**: poll/receive outside `d_lock`, process each datagram under it, then notify the
  upper half after releasing it. The drain budget is 16 datagrams per iteration so continuous
  ingress cannot indefinitely postpone timers. Timers run under the same device lock.
- **Accounting**: peer TX bytes and TX timestamps count admission to the output queue, not
  confirmed backend completion or remote delivery. Backend send failures increment device
  TX errors. Nonces are consumed during encryption even if admission subsequently fails.
  Queued datagrams retain their endpoint snapshot if configuration changes before send.

## Ownership

| Resource | Owner | Lifetime / protection |
|---|---|---|
| UDP socket | driver; RX thread alone sends/receives | opened on up; closed only after RX exit |
| `plainbuf` / `cryptbuf` | TX and RX protocol processing | shared under `d_lock` |
| `rxbuf` | RX thread | static per device |
| outbound entry | queue, then RX worker | immutable copy; quota includes in-flight entry |
| `txqueue`, `ntx` | TX, RX, shutdown | `d_lock` |
| `rxqueue` | RX producer / upper-half consumer | leaf spinlock `rxlock` |
| keys, peers, sessions, lifecycle flags | TX, RX, ioctl, lifecycle | `d_lock` |
| `wake` / `rxdone` | driver / worker | initialized before thread creation, destroyed after exit |

The plaintext buffer is also RX decryption scratch. Separate contexts do not imply separate
buffers: the common lock and notification outside protocol processing are essential.

## Locking Audit

- Upper-half TX acquires `netdev_lock(dev)`, which locks `dev->d_lock`. RX processing,
  timers, and WireGuard ioctls now use that exact lock. Driver entry points assert ownership
  in assertion-enabled builds. Global `net_lock` is not the protocol-state lock.
- Socket output runs outside the device lock and holds only an immutable owned entry.
  There is no `sending` boolean: a guard on one buffer did not protect the rest of the
  protocol state. Socket input also runs outside the device lock and only writes RX-owned
  storage until processing starts.
- `MSG_DONTWAIT` avoids buffered-UDP queue-space waits; it is not a universal bounded-latency
  guarantee. usrsock can wait for its daemon, and unbuffered UDP can wait for completion.
  Their wait helpers release their own usrsock/connection/device mutexes, not automatically
  the global network lock. Correctness no longer depends on which mutex a backend releases.
- Up/down are device-locked, including generic `SIOCSIFFLAGS` and the WireGuard ioctl path.
  Stop disables new submissions, wakes the worker, and releases `d_lock` only for the
  bounded exit wait. During that wait TX drops packets, RX skips protocol processing, and
  key/listen-port changes and peer deletion are refused until the thread is reaped.
- `reaping` prevents concurrent callers from consuming the same exit notification or
  destroying its semaphore twice. A timeout retains the socket, semaphores, and queued
  entries; repeated down can finish cleanup. No timeout is permission to close a live socket.
- The upper half is notified outside protocol processing. Direct mode avoids waiting in
  `work_cancel_sync` for a worker that is itself waiting for the caller's `d_lock`.
- VLAN carrier/TX-done fan-out now skips non-Ethernet parents before acquiring global
  `net_lock`. `vlan_register` only accepts Ethernet parents; doing this work for a TUN
  would introduce an unnecessary `d_lock` -> global-lock edge in VLAN-enabled builds.
- `rxlock` is a leaf spinlock used only for queue operations. Allocation/free and upper-half
  notifications are outside its critical sections. Protocol code does not acquire global
  `net_lock` explicitly or perform backend sends while holding `d_lock`.

This is a source-level ownership audit, not proof of all network-stack lock interactions.
Real usrsock stalls, SMP stress, and long-running resource-pressure tests still require
separate evidence. A stalled backend can delay RX/timers; shutdown preserves resources and
returns a timeout rather than forcibly closing its socket.

## ABI update semantics and constraints

- Private key and listen port change **only while down** (`-EBUSY` otherwise) — sessions are
  bound to them. Address may be set; up/down are requested via flags.
- Peers may be **added or updated at any time** (`SIOCSWGPEER`, under `d_lock`). **Deleting** a
  peer is refused while up or stopping (`-EBUSY`, sessions may be in flight); enumerate is `SIOCGWGPEER` by
  index.
- The **private key is write-only**: `SIOCGWGIF` never returns it (it returns the derived public
  key). It still lives in the user-side config file, which the `wg` command treats as the source
  of truth — so "never leaves the kernel" would be inaccurate.
- Structures are flat and fixed-size; the kernel reads/writes the caller's copy directly, which
  is why they carry no pointers (allowed-ips is a counted fixed array).

## Time and randomness

- TAI64N (`wg_tai64n`) is built from the **monotonic** system clock, truncated to tick
  resolution. This is insufficient across reboot: a peer may reject a restarted initiator's
  lower timestamp. RTC/persistence/rollback policy remains a pre-merge blocker in issue #14.
- Key material and nonces require a real RNG: `NET_WIREGUARD` depends on `CRYPTO_RANDOM_POOL` or
  `DEV_URANDOM_ARCH`, and the constant-seeded software PRNGs are refused in Kconfig.

## Build-type scope (what is actually verified)

| Build | State |
|---|---|
| FLAT (sim) | protocol/crypto verified against Linux kernel WireGuard (T1, TR) |
| KERNEL (`rv-virt:knetnsh64`) | full tunnel verified across the syscall boundary, `wg` a separate ELF (T6) |
| KERNEL build only (`qemu-armv7a:knsh`) | compiles/links (apps/kernel symbol split) |
| PROTECTED | not separately exercised |

The `d_lock`/`rxlock` scheme above is designed to be build-type independent, but only FLAT
(sim) and one KERNEL vehicle (rv-virt) have actually been run; see the matrix for the rest.

## Review History and Evidence

The earlier `a5d2a07b73` / `0c1bf89de2` fixes added nonblocking send flags, stop-timeout
recovery, and a `sending` guard. Their reported sim PASS results did not establish mutual
exclusion: TX and RX used different locks. The queued-output revision above supersedes
that concurrency argument.

The lifecycle test now validates target-side exit statuses with exact result lines
(input echo is excluded), checks ordering, and verifies stopped/restored tunnel traffic
on every cycle. `test-nsh-status.py` covers false-positive and incomplete-output cases.
The flood remains active for down/up commands but is paused for each recovery ping;
this tests lifecycle recovery, not uninterrupted service during an unbounded DoS.
The stop-recovery test requires its test-only Kconfig option; a skipped run is not coverage.
Its `output` mode uses `CONFIG_NET_WIREGUARD_DEBUG_TX_STALL` to retain a datagram outside
the lock, run concurrent control/TX operations, check the retained bytes, and exercise
timeout/reap/re-up. This is an injected worker stall, not a real usrsock daemon test.
Current revision results and remaining gaps: [locking-followup.md](locking-followup.md).

## Open items a reviewer may want to probe

- **TAI64N across reboot/time-rollback** — the handshake timestamp uses the monotonic (since-boot)
  clock, so a board that reboots can be rejected by the responder until its clock passes the last
  value seen. A design decision (RTC / persistence / rollback detection) is still open; tracked
  separately (see the TAI64N issue).
- No `TZ` (zeroization) evidence yet: sessions are cleared on `wg_ifdown` (`wg_peer_clear_sessions`)
  and `wg_set_if` re-inits the device, but a `gcore` check for lingering key bytes is not done,
  and whether the *static* private key should also be wiped on down (it is kept for re-up) is a
  design question.
- Review the device-lock/output-queue audit above, especially backend stalls and SMP lock
  interactions. Sim success alone is not evidence of those paths.
- Repeated `down`/`up` and endpoint churn (plan T7) have a stress test now but not a long soak.
