# In-kernel WireGuard — implementation design

What the in-kernel driver actually does, read from the code (fork `net-wireguard`
`677c8f54fe…`), so a reviewer can judge concurrency, ownership, and cleanup without reading
every line. This describes the **implementation**; planned-but-unverified behaviour is called
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
   `wg_receive` / `wg_ioctl`. `wg_ioctl` takes `net_lock()` itself for the whole switch; the
   others run in the upper half's own net-locked context.
2. **The receive kernel thread** `wg_rx` (one per interface, priority
   `CONFIG_NET_WIREGUARD_RXPRIORITY`, stack `CONFIG_NET_WIREGUARD_RXSTACKSIZE`, default 6144).
   Created in `wg_ifup`, exits in `wg_ifdown`.

## Lifecycle / state transitions

```
register (drivers_initialize)      wg_ifup                       wg_ifdown
  netdev_lower_register(wgN)   ->   psock_socket + psock_bind ->  running=false; wait rxdone
  no socket, no thread              start wg_rx thread            psock_close; clear sessions;
  (nothing allocated)               bifup=true, running=true      drain rxqueue; bifup=false
```

- At **registration** the device exists but owns no socket and no thread; nothing is allocated
  until it is brought up (matches the Spresense rcS order: the usrsock Wi-Fi daemon is up
  before `wg up`, so the UDP socket is opened while usrsock is alive).
- **`wg_ifup`** refuses without a private key (`-EINVAL`); opens the UDP socket bound to the
  listen port on `INADDR_ANY`; arms handshakes for peers that have a configured endpoint;
  then starts the `wg_rx` thread. Every failure path unwinds what it created (close the socket,
  clear `running`) before returning the errno.
- **`wg_ifdown`** clears `bifup`, drops carrier, sets `running=false`, and waits for the RX
  thread to actually exit before touching the socket or the semaphore. The thread checks
  `running` in both its outer loop and its inner drain loop and posts `rxdone` as its last act,
  so it leaves within a poll period; `wg_ifdown` retries the bounded wait and only then closes
  the socket, clears `rxpid`, destroys `rxdone`, wipes every peer's session keys
  (`wg_peer_clear_sessions`), resets endpoints, and drains the RX queue. The net lock is released
  while waiting (so the thread can take it to finish). On the unexpected event that the wait
  still times out, it does **not** close the socket from under a live thread — it leaves the
  interface stopping (`rxpid` set) and returns an error; `wg_ifup` then refuses to re-up until
  the stop completes.

## Data path

- **TX** (`wg_transmit`, upper half): copy the outbound IP packet into `priv->plainbuf`,
  validate it is IPv4 and fits, pick a peer by longest-prefix match on the destination
  (`wg_peer_for_dest`), then `wg_send_data` encrypts into `priv->cryptbuf` and `psock_sendto`s
  it. With no usable session the packet is dropped and a handshake is requested (rate-limited to
  `WG_REKEY_TIMEOUT`); the stack retransmits.
- **RX** (`wg_rx` thread): `psock_poll` with a `WG_TIMER_MSECS` timeout (a poll callback posts a
  local semaphore), then drain every datagram with `psock_recvfrom(MSG_DONTWAIT)` into
  `priv->rxbuf`, processing each under `net_lock()` in `wg_process_datagram`. Decrypted transport
  packets are handed to the stack via `wg_deliver`, which queues them on `priv->rxqueue` and
  calls `netdev_lower_rxready`; the upper half then pulls them with `wg_receive`. After draining,
  the thread runs `wg_run_timers` (also under `net_lock`).

## Ownership

| Resource | Owner | Lifetime |
|---|---|---|
| `priv->psock` (UDP `struct socket`) | the driver (not an fd table) | `wg_ifup` → `wg_ifdown` |
| `wg_rx` thread | the driver | `wg_ifup` → `wg_ifdown` |
| `priv->plainbuf` / `cryptbuf` (TX) | TX (upper-half) only | static in the device |
| `priv->rxbuf` (RX) | the `wg_rx` thread only | static in the device |
| `priv->rxqueue` | shared (thread fills, upper half drains) | guarded by `priv->rxlock` |
| `priv->wg` (keys, peers, sessions) | shared (ioctl, RX thread, TX, timers) | guarded by `net_lock` |

TX and RX use **separate** static scratch buffers, so the two contexts never write the same
buffer (an earlier RX/TX aliasing bug was fixed by giving RX its own `rxbuf`).

## Locking

- **`net_lock()` serializes all protocol-state access.** `wg_ioctl` takes it for the whole
  switch; the RX thread takes it around datagram processing and around `wg_run_timers`; TX runs
  in the upper half's net-locked context. So configuration changes, inbound processing, timer
  work, and outbound send never touch `priv->wg` concurrently.
- **Sends are non-blocking (`MSG_DONTWAIT`)** so a send never drops `net_lock` mid-way. This
  matters: a blocking UDP send releases the network lock while it waits for a write buffer
  (`udp_sendto_buffered`), which would let another `net_lock` holder overwrite the shared
  `cryptbuf` or mutate the keypair while a send is in flight. Non-blocking keeps each send atomic
  under `net_lock`; on back pressure the datagram is dropped (peer/stack retransmit). Fixed after
  a design review (fork `a5d2a07b73`).
- **`priv->rxlock`** (spinlock, IRQ-save) guards only the short critical sections that add to or
  remove from `priv->rxqueue`, since that queue is touched from the thread, the upper half, and
  `wg_ifdown`. It is a leaf lock (never taken while blocking, never nests another lock).
- **Lock order:** `net_lock` is the single ordering lock; `rxlock` is a leaf spinlock taken only
  for the queue. No lock is acquired while another is held except `rxlock` under nothing. The RX
  thread never holds `net_lock` across a blocking call (it drops it before `psock_recvfrom`);
  `wg_ifdown` drops `net_lock` only while waiting for the thread to exit.

## ABI update semantics and constraints

- Private key and listen port change **only while down** (`-EBUSY` otherwise) — sessions are
  bound to them. Address may be set; up/down are requested via flags.
- Peers may be **added or updated at any time** (`SIOCSWGPEER`, under `net_lock`). **Deleting** a
  peer is refused while up (`-EBUSY`, sessions may be in flight); enumerate is `SIOCGWGPEER` by
  index.
- The **private key is write-only**: `SIOCGWGIF` never returns it (it returns the derived public
  key). It still lives in the user-side config file, which the `wg` command treats as the source
  of truth — so "never leaves the kernel" would be inaccurate.
- Structures are flat and fixed-size; the kernel reads/writes the caller's copy directly, which
  is why they carry no pointers (allowed-ips is a counted fixed array).

## Time and randomness

- TAI64N (`wg_tai64n`) is built from the **monotonic** system clock, truncated to tick
  resolution — it does not run backwards, which is what the responder's monotonic-increase
  requirement needs. Reboot re-handshake scenarios (plan TT) are not yet tested.
- Key material and nonces require a real RNG: `NET_WIREGUARD` depends on `CRYPTO_RANDOM_POOL` or
  `DEV_URANDOM_ARCH`, and the constant-seeded software PRNGs are refused in Kconfig.

## Build-type scope (what is actually verified)

| Build | State |
|---|---|
| FLAT (sim) | protocol/crypto verified against Linux kernel WireGuard (T1, TR) |
| KERNEL (`rv-virt:knetnsh64`) | full tunnel verified across the syscall boundary, `wg` a separate ELF (T6) |
| KERNEL build only (`qemu-armv7a:knsh`) | compiles/links (apps/kernel symbol split) |
| PROTECTED | not separately exercised |

The `net_lock`/`rxlock` scheme above is designed to be build-type independent, but only FLAT
(sim) and one KERNEL vehicle (rv-virt) have actually been run; see the matrix for the rest.

## Design-review fixes (2026-09-21, fork `a5d2a07b73`)

A review (Codex, issue #12) surfaced two real concurrency defects, both fixed and covered above:
non-blocking sends (the `net_lock`-drop-mid-send window) and a reliable `wg_ifdown` stop with a
re-up guard (the close-under-a-live-thread window). A lifecycle stress test
(`scripts/kernel/verify-sim-wg-downup.sh`: 25 down/up cycles under an inbound flood) and the full
functional regression (T1/TF/TR/TN/T3) pass with the fixes. **Honest limitation:** both defects
are races that the sim did not trip deterministically — the pre-fix driver also passed the stress
test — so the fixes rest on the code analysis above, not on a demonstrated pre-fix failure.

## Open items a reviewer may want to probe

- **TAI64N across reboot/time-rollback** — the handshake timestamp uses the monotonic (since-boot)
  clock, so a board that reboots can be rejected by the responder until its clock passes the last
  value seen. A design decision (RTC / persistence / rollback detection) is still open; tracked
  separately (see the TAI64N issue).
- No `TZ` (zeroization) evidence yet: sessions are cleared on `wg_ifdown` (`wg_peer_clear_sessions`)
  and `wg_set_if` re-inits the device, but a `gcore` check for lingering key bytes is not done,
  and whether the *static* private key should also be wiped on down (it is kept for re-up) is a
  design question.
- Concurrency is now argued from `net_lock` **plus non-blocking sends**; a full audit confirming
  every `priv->wg` access holds net_lock remains worthwhile.
- Repeated `down`/`up` and endpoint churn (plan T7) have a stress test now but not a long soak.
