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
- **`wg_ifdown`** clears `bifup`, drops carrier, sets `running=false`, and waits on `rxdone`
  (up to `WG_STOP_WAIT_MSECS`) for the thread to finish its current iteration and post it — the
  net lock is released while waiting because the thread needs it. Then it closes the socket,
  clears every peer's session keys and handshake state (`wg_peer_clear_sessions`), resets
  endpoints to the configured ones, and frees anything left in the RX queue.

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
- **`priv->rxlock`** (spinlock, IRQ-save) guards only the short critical sections that add to or
  remove from `priv->rxqueue`, since that queue is touched from the thread, the upper half, and
  `wg_ifdown`.

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

## Open items a reviewer may want to probe

- No `TZ` (zeroization) evidence yet: sessions are cleared on `wg_ifdown` (`wg_peer_clear_sessions`)
  and `wg_set_if` re-inits the device, but a `gcore` check for lingering key bytes is not done.
- Concurrency is argued from `net_lock`; a review of every `priv->wg` access confirming it holds
  net_lock (especially any path reachable without it) is worthwhile.
- Repeated `down`/`up` and endpoint churn (plan T7) are not soak-tested.
