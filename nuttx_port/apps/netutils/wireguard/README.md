# apps/netutils/wireguard

WireGuard for NuttX, implemented as a `wg0` network device.

This directory is laid out exactly as it would be submitted to
`apache/nuttx-apps`, so the tree here and the tree in a pull request are the
same thing.

## Which files are ours and which are not

**Third party, taken unmodified from
[wireguard-lwip](https://github.com/smartalock/wireguard-lwip)**
(BSD-3-Clause, Copyright (c) 2021 Daniel Hope). These keep their original
license headers and are byte-identical to upstream:

| File | Contents |
|---|---|
| `wireguard.c` / `wireguard.h` | Protocol: handshake, key schedule, transport messages |
| `crypto.c` / `crypto.h` | Dispatch to the primitives below |
| `crypto/refc/blake2s.*` | BLAKE2s (from RFC 7693) |
| `crypto/refc/chacha20*.` | ChaCha20 and ChaCha20-Poly1305 |
| `crypto/refc/poly1305-donna*` | Poly1305 (public domain or MIT, Andrew Moon) |
| `crypto/refc/x25519.*` | Curve25519 (MIT, Cryptography Research Inc.) |
| `LICENSE.wireguard-lwip` | The upstream project's license text |

**Third party, adapted.** Upstream expects each port to supply this one -
its own comment says "Your platform integration needs to provide
implementations of these functions". Only the peer and allowed-ip limits
changed, to come from Kconfig; the license notice and the prototypes are
untouched:

| File | Contents |
|---|---|
| `wireguard-platform.h` | The four OS hooks, plus the static limits |

**Written for NuttX:**

| File | Contents |
|---|---|
| `nuttx-platform.c` | The four OS hooks: monotonic time, entropy, TAI64N, load |
| `nuttx-wireguardif.c` | `wg0` as a `NET_LL_TUN` netdev over a UDP socket |
| `nuttx-wireguardif.h` | Its interface |
| `wg_main.c` | The `wg` NSH command |
| `Kconfig`, `Makefile`, `Make.defs`, `CMakeLists.txt` | Build integration |
| `lwip/*.h` | Compatibility shims, see below |

## The `lwip/` directory

`wireguard.h` includes four lwIP headers for types it needs -
`u8_t`/`u16_t`/`u32_t`, `ip_addr_t`, and forward declarations of
`struct netif` and `struct udp_pcb`. NuttX has no lwIP, so these shims
supply just those, in about fifteen lines each.

The alternative is to patch those includes out of `wireguard.h`. That would
remove a directory whose name is misleading inside a NuttX app, at the cost
of making a second vendored file diverge from upstream. Keeping the shims
was chosen so the protocol and crypto sources stay byte-identical and easy
to re-diff against upstream, but this is worth settling in review rather
than assuming.

## What was deliberately left out

- `wireguardif.c` / `wireguardif.h` - the lwIP netif glue, which
  `nuttx-wireguardif.c` replaces
- `crypto/cortex/` - an ARM assembly X25519 this port does not select

Both are unused, and carrying them would widen what `nuttx-apps`' `LICENSE`
has to enumerate for no benefit.

## Notes for the LICENSE file

`apache/nuttx-apps` lists bundled third-party code in its top-level
`LICENSE`. The text to add is drafted in
[`docs/license-appendix-draft.md`](../../../../docs/upstream/license-appendix-draft.md),
with each file's license checked against the file itself rather than
assumed.
