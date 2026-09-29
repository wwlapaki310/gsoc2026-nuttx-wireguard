# Upstream PR description drafts (English)

Ready-to-paste PR bodies for the upstream submissions. **Drafts** — the author opens the
actual PRs. Order (per [in-kernel-plan.md](in-kernel-plan.md) §4.1): the `crypto:` nonce PR
first (the driver depends on it), then the driver PR, then the apps PR.

- **crypto: nonce fix** — the body is [chachapoly-nonce-draft.md](chachapoly-nonce-draft.md).
- **PR-K1 (driver)** — below.
- **PR-A1 (apps)** — below.

Last updated: 2026-09-29. Local submission branches `review/pr-series-20260928`:
NuttX `8defcefa94` on `68dd87f4df`, apps `c039b232e5` on `b66303e2`.
Original development branches remain unchanged. These drafts do not authorize
fork publication; final validation and source scope are recorded separately.

---

## PR-K1 — `net/wireguard: add an in-kernel WireGuard network device`

Target: **apache/nuttx**. Depends on the `crypto:` chachapoly nonce fix (below/first).

### Summary

Adds WireGuard as a virtual network device `wg0`. IP packets routed to `wg0` are encrypted and
sent to the peer over UDP; UDP datagrams arriving on the listen port are decrypted and injected
into the stack as if received on `wg0`. The peer can be any WireGuard implementation (the Linux
kernel module, wireguard-go, boringtun, or another NuttX). `wg0` is a `NET_LL_TUN` netdev
registered with the `netdev_lowerhalf` framework — NuttX has its own TCP/IP stack, so this is
not an lwIP netif.

The cryptography uses NuttX's own `crypto/` (BLAKE2s, ChaCha20-Poly1305, Curve25519). The
protocol core (Noise_IKpsk2 handshake, key derivation, anti-replay window, load-time cookie) is
ported from wireguard-lwip (BSD-3-Clause) into NuttX style.

### Design

- **`netdev_lowerhalf` + a kernel-owned UDP socket + an RX kthread.** The driver holds one
  `struct socket` (UDP); a dedicated RX kernel thread does `psock_poll` → recv → decrypt →
  `ip_input`. TX encrypts from the lower-half transmit path and sends over UDP. Precedent:
  `net/rpmsg/rpmsgdrv.c` (a driver that owns a socket + kthread).
- **Backend-independent queued output.** Protocol state is under the device `d_lock`; outbound
  datagrams are encrypted into a bounded immutable queue; only the RX thread calls `psock_sendto`,
  outside `d_lock` and without live peer/keypair references. This keeps sends correct on
  buffered UDP, unbuffered UDP, and usrsock (which ignores `MSG_DONTWAIT`).
- **Flat, pointer-free ioctl ABI** (`include/nuttx/net/wireguard.h`). Under PROTECTED/KERNEL the
  kernel copies the caller's struct directly, so no embedded pointers; variable-length data
  (allowed-ips) uses fixed-cap arrays. Dispatched from `netdev_ioctl.c` directly (not registered
  in `net_ioctl_arglen()`), so the usrsock daemon does not intercept it.
- **Private key is write-only.** `SIOCSWGIF` accepts it; `SIOCGWGIF` never returns it (it returns
  the derived public key). The key still lives in the user-side config file (the source of
  truth), so this is "the get ioctl does not return the key," not "key material never leaves the
  kernel."
- Anti-replay window 2048 (Linux 8192, the reference 32) for Wi-Fi reordering tolerance.

### ioctl ABI

Five commands on an `AF_INET` socket (`0x0046..0x004A`): `SIOCSWGIF`/`SIOCGWGIF`
(`struct wg_ifreq_s`: private key write-only / listen port / tunnel address / up-down; get
returns the derived public key) and `SIOCSWGPEER`/`SIOCDWGPEER`/`SIOCGWGPEER`
(`struct wg_peerreq_s`: add-or-update / delete / enumerate; get returns endpoint, allowed-ips,
keepalive, and stats). Private key, listen port, and `setconf` are only accepted while `wg0` is
down.

### Dependencies

Requires the `crypto:` ChaCha20-Poly1305 u64-nonce fix (submitted first). Without it the
handshake completes but transport data past the first packet fails to decrypt.

### Testing

Against a real Linux kernel WireGuard peer unless noted (see the
[verification matrix](verification-matrix.md) for the full table):

- **CI compile path** — `boards/sim/sim/sim/configs/wireguard/defconfig`
  (`./tools/configure.sh sim:wireguard`).
- **sim runtime** — bidirectional handshake, tunnelled ping, `saveconf`/`setconf` round-trip,
  on-device genkey, pubkey matching `wg(8)`; two simultaneous peers; ioctl negative cases; wrong
  key / PSK mismatch rejected; replay does not move the endpoint; an initiation flood draws
  cookie replies and the tunnel survives.
- **Real kernel build** — `rv-virt:knetnsh64` (BUILD_KERNEL + virtio-net) tunnels bidirectionally
  to Linux kernel WireGuard over a host TAP, with `wg` loaded as a separate ELF across the
  syscall boundary. `qemu-armv7a:knsh` also builds under BUILD_KERNEL.
- **Hardware on earlier source revisions** — both ESP32-S3 native Wi-Fi and
  Spresense GS2200M/usrsock (FLAT) tunnel to the Windows client. Spresense's
  clean-build boot issue was isolated to the cxd56 RTC_HIRES regression (#9),
  not a Docker cache requirement. Later Spresense runs measured resource use,
  lifecycle recovery under injected delay, and the clock-unset failure/recovery.
  These hardware observations are not a rerun of the rebased submission series.
- **PROTECTED and SMP** — the rebased candidate passed rv-virt protected
  userspace ioctl/tunnel runtime and a four-CPU kernel concurrency scenario.
  They do not establish silicon MPU behavior or exhaustive race freedom.
- **Crypto KATs** — ChaCha20-Poly1305 (u64 counter), XChaCha20-Poly1305 (cookie path, incl.
  HChaCha20), X25519 (RFC 7748), BLAKE2s-256, against NuttX's `crypto/` sources.

A build-config fix was needed for boards that default `CONFIG_IOB_NCHAINS=0`: the driver's
`netpkt_queue_t rxqueue` is `struct iob_queue_s`, which only exists for `IOB_NCHAINS > 0`.
`mm/iob/Kconfig` now carries `default IOB_NBUFFERS if NET_WIREGUARD`.

A BUILD_KERNEL-only bug was found and fixed during bring-up: `wg_set_if()` placed a
~6 KB `struct wg_peer_s` snapshot on the 3 KB kernel stack (sim's larger stacks hid it) — moved
to `kmm_malloc`/`kmm_free`.

### Known limitations (honest)

- **TAI64N across reboot** — uses realtime plus an in-RAM same-boot high-water
  mark. Platforms must establish reliable time; an unset/reset clock can still
  prevent NuttX-initiated reconnection to a responder retaining the old timestamp.
  Durable range reservation is deliberately unimplemented; #14 remains open.
- **Unverified conditions** — real backend blocking/AP loss, storage power cuts,
  day-scale operation, hardware PROTECTED, and ESP32-S3 resource measurements.
  Sim now samples nonzero current/previous/next and handshake secrets before
  down and their zeroization after down, including one natural rekey cycle.
  Entropy tests used board reset, not cold power cycling or entropy-quality analysis.

### Commit structure

Two commits on this branch: the `crypto:` nonce fix (submitted as its own PR first) and the
`net/wireguard` device (ABI + driver + crypto shim + `sim:wireguard` defconfig + Documentation).

---

## PR-A1 — `system/wg: add a WireGuard configuration command`

Target: **apache/nuttx-apps**. Depends on PR-K1 (the ioctl ABI).

### Summary

Adds the `wg` NSH command, the userspace client for the in-kernel WireGuard device (PR-K1). It
configures `wg0` over the ioctl ABI and manages a `wg(8)`-compatible configuration file.

Subcommands: `up`, `down`, `show`, `showconf`, `setconf`, `saveconf`, `set`, `genkey`, `pubkey`.
Because the driver never returns the private key, the command treats the `wg(8)`-format config
file as the source of truth: `set`/`setconf` push to the device, `saveconf` writes from the
file side.

Since that file is the only copy of the key, persistence failures are surfaced rather than
swallowed: `wg set private-key` exits nonzero and warns if the device took the key but it could
not be written. Because NuttX VFS can unlink the destination inside `rename`,
publication first reserves an exclusive recovery copy of the old configuration.
Failure preserves the backup and new staging file and blocks another publisher
until explicit recovery. Staging uses exclusive mode-0600 temporary files.
This handles tested ordinary failures, not an unverified power-loss durability
contract; recovery and space overhead are documented.

### Offline key generation

`genkey`/`pubkey` run without the device. `genkey` only needs `/dev/urandom`, and `show` takes the
interface's public key from the driver, which derives it in the kernel — so the only thing needing
Curve25519 in userspace is the offline `pubkey` subcommand. That is a single vendored MIT file,
`wg_x25519.c`.

Cryptodev can do it: `CRK_DH_COMPUTE_KEY` is implemented as `curve25519()` in `cryptosoft.c`. The
reason not to use it is the dependency — those handlers are compiled only for
`CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO` (which `depends on CRYPTO_SW_AES`) or
`..._SOFTWARE_KEYMGMT` (which needs an MTD config device), so one scalar multiplication would pull
the whole software cipher suite into the image and make `wg` unbuildable where cryptodev is off.
Upstream `wireguard-tools` vendors its own curve25519 for the same reason. The two implementations
are cross-checked in testing: `wg pubkey <priv>` must equal the `public key:` line of `wg show`,
which comes from the kernel.

The algorithm body keeps its upstream MIT formatting so it stays diffable against the STROBE
source; the path is listed in `tools/nxstyle.c`'s `g_white_files[]` the way the PHY62XX, Infineon
and GD32VW55x vendor sources are. **That one line lands with PR-K1, so this PR's style check
depends on PR-K1 going in first.**

### Testing

Exercised together with PR-K1 on sim and on the `rv-virt:knetnsh64` real kernel build (loaded as
a separate ELF across the syscall boundary). `genkey` output's public key matches `wg(8)`.

### Commit structure

One commit: `apps/system/wg`.

---

> Rebased local submission branches are prepared. The author still reviews the
> series, adds any required author certification, pushes forks and opens PRs.
