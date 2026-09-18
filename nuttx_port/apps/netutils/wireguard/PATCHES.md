# Local patches to the vendored wireguard-lwip sources

The protocol core (`wireguard.c` / `wireguard.h`) and the reference
crypto (`crypto.c`, `crypto/refc/*`) come from
[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip)
(BSD-3-Clause). Until 2026-09-18 they were carried byte-identical to
upstream. The changes below were made after a protocol/security review of
the port; each is marked in the source with a `// NuttX patch N:` comment
and is intended to be offered upstream.

| # | File | Change | Why |
|---|---|---|---|
| 1 | `wireguard.c` `wireguard_process_initiation_message()` | `rate_limit = (peer->last_initiation_rx != 0) && ((now - peer->last_initiation_rx) < ...)` (was `last - now`) | The subtraction was reversed: `last - now` wraps to a large unsigned value whenever `last < now`, so the per-peer initiation rate limit never applied. The `!= 0` guard keeps the first initiation after boot from being rate-limited against the "never" value. |
| 2 | `wireguard.h` `struct wireguard_keypair`, `wireguard.c` `wireguard_check_replay()` / `wireguard_start_session()` | `replay_bitmap` is `uint64_t[WIREGUARD_REPLAY_WORDS]` (default 2048 bits) instead of one `uint32_t` (32 bits) | A 32-packet window drops genuine traffic as soon as Wi-Fi retransmission or a busy receive thread reorders more than that. Same RFC 2401 sliding-window algorithm, wider. Size is `CONFIG_NET_WIREGUARD_REPLAY_WINDOW`. |

`crypto.c` and `crypto/refc/*` remain byte-identical to upstream (the
`wg_` symbol prefix applied by the build is a compile-time rename, not a
source change).

Not patched, handled in the NuttX glue instead (see `nuttx-wireguardif.c`):
the under-load decision and cookie exchange (the core never calls
`wireguard_is_under_load()` itself), the replay check ordering relative to
endpoint updates, and session teardown on interface down.
