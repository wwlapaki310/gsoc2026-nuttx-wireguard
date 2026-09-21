# dev@nuttx.apache.org 投稿ドラフト（カーネル版）

Issue #3 用。PR を出す前に設計の合意を取るためのメール本文案。**これは草稿であり、投稿は本人が行う**
（役割分担: upstream への push / PR / dev@ 投稿は本人。実装・検証・文書はこのリポジトリ）。

**送る前に確認すること:**

- メンター (Alan Carvalho de Assis) に先に見てもらう
- 実測値・リンク・fork の公開状態が最新か（本文は fork が push 済みである前提のリンクを含む）
- 件名の `[DISCUSS]` プレフィックスが慣例か確認する
- 旧ドラフト（apps/netutils、FLAT 制約、単一ピア、Spresense 未通信）は履歴。**現行はカーネル版**

> 旧ドラフトからの変更点: 設計は「apps に置いた netdev」ではなく **`drivers/net/wireguard/` の
> カーネル内 netdev + ioctl クライアント**。旧ドラフトが相談していた FLAT 制約は、ソケットを
> カーネル所有にすることで解消済み。

---

## 件名

```
[DISCUSS] In-kernel WireGuard for NuttX: a NET_LL_TUN netdev + ioctl ABI
```

## 本文

```
Hi all,

I have an in-kernel WireGuard implementation for NuttX that I would like to
send upstream, and before opening PRs I would like to check the design with
the list -- mainly the ioctl ABI and the placement.

Short version: "wg0" is a NET_LL_TUN netdev in drivers/net/wireguard/. Its
"wire" is a UDP socket owned by the driver, serviced by a receive kernel
thread. It is configured at run time through a small, fixed-size ioctl ABI,
driven by a user-space "wg" command in apps/system/wg. The cryptography is
NuttX's own crypto/ (BLAKE2s, ChaCha20-Poly1305, Curve25519); only the
protocol state machine is ported, from wireguard-lwip (BSD-3-Clause).

(This supersedes an earlier apps/netutils/wireguard prototype of mine that
depended on psock_*() internals and only worked in FLAT builds. Moving the
device into the kernel is what resolved that restriction.)


## Placement and data path

wg0 registers as a lower-half netdev (NET_LL_TUN), modelled on the way a
driver owns its resources:

  TX: stack -> transmit() -> encrypt -> sendto() on the kernel UDP socket
  RX: a kernel thread blocks on the UDP socket -> decrypt -> ip_input()

The UDP socket and the RX thread are created in ifup() and torn down in
ifdown(), not at registration, so nothing is allocated until the interface
is brought up. Holding the socket in the kernel (as a struct socket) is what
makes this work in PROTECTED and KERNEL builds, where the earlier apps-side
approach could not. Precedent for a kernel driver that owns a socket and a
thread: net/rpmsg/rpmsgdrv.c.


## The ioctl ABI (the main design question)

Keys and peers are set at run time with five commands on an AF_INET socket:

  SIOCSWGIF  / SIOCGWGIF   - interface: private key, listen port, address, up/down
  SIOCSWGPEER / SIOCDWGPEER / SIOCGWGPEER - add-or-update / delete / enumerate a peer

The structures (include/nuttx/net/wireguard.h) are flat and pointer-free
(variable-length allowed-ips is a fixed-size array with a count), because in
PROTECTED/KERNEL builds the kernel reads and writes the caller's structure
directly and a pointer inside it could not be validated.

Two choices I would like feedback on:

  1. These SIOC commands are dispatched from netdev_ioctl.c but are
     deliberately NOT registered in net_ioctl_arglen(). If they were, the
     usrsock daemon would forward them and intercept the device; unregistered
     SIOC commands are passed through (usrsock returns -ENOTTY). This depends
     on the usrsock behaviour and is related to a separate fix I have for
     ifname-scoped ioctls (I can send that first as its own patch).

  2. The private key is write-only: SIOCGWGIF never returns it (there is no
     uid in NuttX, so "return it" means "return it to every task"). The "wg"
     command keeps the private key in the wg(8)-format config file as the
     source of truth. Is a write-only key acceptable, or is a
     Kconfig-gated read-back wanted for debugging?

Is a fixed-size ioctl ABI the right shape here, or would the list prefer
something closer to a netlink-style interface for future extension (IPv6,
more peers)?


## A crypto/ bug found on the way

Bringing this up against a Linux kernel WireGuard peer surfaced a bug in
crypto/chachapoly.c: the u64-counter ChaCha20-Poly1305 nonce is placed in
the wrong bytes (0..7 instead of 4..11, RFC 8439 / WireGuard). Counter 0
matches, so a handshake completes and every transport-data packet after the
first fails to decrypt. There is no in-tree caller today, so it had never
been exercised. I will send this as a small crypto: patch (with a KAT for
crypto/testmngr.c) before the WireGuard patches, since they depend on it.


## Verification

  - sim, against a Linux kernel WireGuard peer: handshake, tunnelled ping,
    saveconf/setconf round-trip, on-device genkey, and negative paths
    (a replayed packet does not move the peer endpoint; an initiation flood
    past the load threshold draws cookie replies).
  - A real kernel build: rv-virt:knetnsh64 (BUILD_KERNEL + virtio-net) in
    QEMU, a full bidirectional tunnel to Linux kernel WireGuard, with the
    "wg" command loaded as a separate ELF across the syscall boundary.

An earlier FLAT/apps prototype of the same protocol is what I verified on
real hardware (ESP32-S3 and Spresense over real Wi-Fi, against the official
Windows client); re-verifying the in-kernel driver on hardware is still
ahead. IPv6, KAT and a few negative/soak tests are also still open -- I am
tracking these honestly rather than claiming them.


## PR plan

If the design is acceptable I will split it as CONTRIBUTING suggests:

  1. crypto: the ChaCha20-Poly1305 nonce fix (+ KAT), on its own.
  2. net/wireguard: the device, the ioctl ABI, crypto shim, protocol core,
     a sim config, and Documentation.
  3. apps/system/wg: the ioctl client.

Design write-up, ABI header, and the verification matrix: [1]

Thanks for any feedback.

[1] <repository / doc URLs once public>
```

---

## 補足: 想定される質問と答え

| 質問 | 答え |
|---|---|
| なぜ `drivers/net/` なのか（旧案は apps） | カーネルが UDP ソケットと RX スレッドを所有することで PROTECTED/KERNEL でも動く。FLAT 専用だった旧 apps 版の制約を解消 |
| ioctl が usrsock に横取りされないか | `net_ioctl_arglen()` に**登録しない**ので usrsock は `-ENOTTY` で素通し。ifname スコープの ioctl 修正(#10 相当)は独立パッチとして先行可能 |
| 秘密鍵の扱い | write-only。`SIOCGWGIF` は返さない。正本は `wg(8)` 形式の設定ファイル。デバッグ用の読み戻しは Kconfig ゲートにするか要相談 |
| ABI は固定長 ioctl でよいか | 相談点。将来の IPv6・ピア増を見据えると netlink 風の是非も含めて意見を求める |
| crypto は | NuttX 純正 `crypto/`。プロトコル本体のみ wireguard-lwip(BSD-3-Clause)由来。ユーザー空間の offline genkey/pubkey に MIT の X25519 を1ファイル同梱 |
| IPv6 は | 未対応(現状 IPv4) |
| 検証は | sim + rv-virt:knetnsh64(BUILD_KERNEL)を Linux カーネル WireGuard 相手に。実機は旧 apps 版で実施済み、カーネル版の実機は未了。詳細は [verification-matrix.md](verification-matrix.md) |
