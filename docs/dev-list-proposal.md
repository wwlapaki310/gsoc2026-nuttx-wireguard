# dev@nuttx.apache.org 投稿ドラフト

Issue #3 用。PR を出す前にアーキテクチャの合意を取るためのメール本文案。

**送る前に確認すること:**

- メンター (Alan Carvalho de Assis) に先に見てもらう
- 実測値・リンクが最新か
- 件名は `[DISCUSS]` プレフィックスが慣例か確認する

---

## 件名

```
[DISCUSS] WireGuard VPN support for NuttX as a netdev (apps/netutils/wireguard)
```

## 本文

```
Hi all,

I have been working on bringing WireGuard VPN support to NuttX, and before
sending patches I would like to check the core design decision with the
list, because it is not something NuttX has done before and I would rather
find out now than after the code is written.

Short version: "wg0" is implemented as a NET_LL_TUN netdev whose "wire" is
a UDP socket, rather than as a driver in the kernel tree. I would like to
know whether that placement is acceptable, and how much of a problem the
FLAT-build restriction it currently carries would be.


## What it is

WireGuard is a VPN that fits embedded targets in a way OpenVPN and IPsec do
not: about 4000 lines, no cipher negotiation (Curve25519 / ChaCha20-Poly1305
/ BLAKE2s are fixed), and a single UDP port. On ESP32-S3 the whole component
measures 18,675 bytes of .text and 4,232 bytes of .bss.

The protocol and crypto come from wireguard-lwip [1] (BSD-3-Clause,
Copyright (c) 2021 Daniel Hope), vendored unmodified. Only the network
interface layer is new, because lwIP's netif / pbuf / udp_pcb have no NuttX
equivalent - wireguardif.c could not be reused at all.


## The design question

wg0 registers with netdev_register(..., NET_LL_TUN) and follows the
drivers/net/tun.c pattern, but instead of a character device on one side it
has a UDP socket:

  TX: stack -> d_txavail -> devif_poll -> encrypt -> psock_sendto()
  RX: psock_recvfrom() -> decrypt -> ipv4_input() back into the stack

So it is an apps/netutils component that registers a network device. That is
the part I am unsure about. The alternatives I considered:

  - drivers/net/: would fit the "registers a netdev" shape better, but the
    protocol and crypto are a large BSD-licensed body of code that seems
    happier in apps/, and it needs a UDP socket, which drivers do not
    normally open.

  - A tun-based userspace daemon: portable and clean, but doubles the copies
    and context switches on a target where I measured 260 KiB/s of TCP
    throughput to begin with.

I went with the netdev-in-apps approach because it keeps the vendored code
untouched and avoids the extra hop, but I am happy to be told otherwise
while it is still cheap to change.


## The FLAT-build restriction

The TX path is driven from the LPWORK thread (via d_txavail), and the RX
path from a task the component creates. Those are different task groups, and
NuttX file descriptors are scoped per task group - so sendto() on a plain fd
worked from the RX task and failed with EBADF from LPWORK. The symptom was
unpleasant: ping worked, TCP handshakes completed, and then application data
vanished silently, because ICMP replies and SYN-ACKs are built synchronously
inside ipv4_input() on the RX task's own stack while everything else is
queued asynchronously.

The fix was to hold the socket as a struct socket and use psock_sendto() /
psock_recvfrom(), which do not go through the descriptor table. That works,
but psock_*() is an internal API, so the component currently assumes a FLAT
build. PROTECTED and KERNEL builds would need a different approach.

I would appreciate guidance here. Is depending on psock_*() from apps/
acceptable for a component like this? Is there an existing pattern for
crossing that boundary that I have missed?


## Current state

Verified on hardware, not just in simulation:

  - ESP32-S3, real Wi-Fi, against the official WireGuard client for Windows
    (not a Linux kernel peer, so this is cross-implementation): handshake,
    ICMP, TCP (telnet and HTTP through the tunnel), rekey during a 7 MB
    transfer, 260 KiB/s, 33-68 ms RTT under load.
  - Sony Spresense (ARM Cortex-M4F): wg0 comes up; the main board has no
    Wi-Fi so no handshake there.
  - sim and qemu-armv7a against a Linux kernel WireGuard peer.

Runtime configuration follows wg(8): genkey, pubkey, set peer, setconf /
showconf using the same INI format, so config files interchange with desktop
clients.

Known gaps: single peer (the vendored platform header fixes
WIREGUARD_MAX_PEERS at 1), no IPv6, and the FLAT-build question above.

Design write-up with diagrams and the measured numbers: [2]

If the placement is acceptable I will split this into small PRs along the
lines CONTRIBUTING asks for - build system and vendored sources first, then
the platform layer and netdev, then the NSH command.

Thanks for any feedback.

[1] https://github.com/smartalock/wireguard-lwip
[2] <design document URL>
```

---

## 補足: 想定される質問と答え

| 質問 | 答え |
|---|---|
| なぜ `drivers/net/` ではないのか | vendored の BSD コードが大きく、UDP ソケットを開く必要がある。ただし異論があれば移動は可能 |
| なぜ TUN デバイス + ユーザースペースデーモンではないのか | コピーとコンテキストスイッチが倍増する。実測 260 KiB/s が出発点なので影響が大きい |
| IPv6 は | 未対応。vendored 実装が IPv4 前提の箇所がある |
| 複数ピアは | `WIREGUARD_MAX_PEERS=1`。Kconfig 化は Issue #4 |
| ライセンスは | BSD-3-Clause、`ALLOW_BSD_COMPONENTS` でゲート済み。`LICENSE` 追記は Issue #7 |
| テストは | 実機3種 + sim/qemu。ログは `docs/phase4-log.md` |
