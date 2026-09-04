# Phase 2 開発ログ: プラットフォーム層実装と netif 統合

## 目標

`wireguard-platform.h` の4関数を実装し、WireGuard を NuttX 上で実際に動作するネットワークインターフェース (`wg0`) として登録する。`sim:nsh` 上で `ifconfig` に `wg0` が表示されることを確認する。

Phase 1 との違い: Phase 1 はビルドが通ることだけがゴールのスタブだった。Phase 2 では `nuttx-platform.c` と `nuttx-wireguardif.c` を実装で置き換え、`wg0` を実際に起動できる状態にした。

---

## 設計判断

### lwIP netif ではなく NuttX netdev として実装

`wireguardif.c`(参照実装)は lwIP の `netif_add()` / `udp_pcb` / `pbuf` を直接呼んでいるが、NuttX は lwIP を使わない独自スタックを持つ。移植先の型として何を選ぶかを検討した:

- NuttX の `drivers/net/tun.c`(TUN 仮想デバイス)を参照実装として採用。`struct net_driver_s` を `netdev_register(dev, NET_LL_TUN)` で登録し、`d_ifup` / `d_ifdown` / `d_txavail` コールバックと `devif_poll()` / `ipv4_input()` で NuttX のネットワークスタックと接続する構造をそのまま踏襲した。
- 実機の NIC がない代わりに、`wg0` の「配線」は生の UDP ソケット (`socket(AF_INET, SOCK_DGRAM, ...)`) にした。TX 側は平文 IP パケットを `wireguard_encrypt_packet()` で暗号化して `sendto()`、RX 側はバックグラウンドスレッドで `recvfrom()` した暗号文を復号して `ipv4_input()` でスタックに注入する。

### ロック戦略: `net_lock()` を単一の排他制御として使う

NuttX の `net_lock()` は `nxrmutex`(再帰的ミューテックス)実装であることをソースで確認した。そのため:

- TX poll (`wg_txpoll`) はワークキュー側の `net_lock()` の中で実行される。
- RX スレッド側もパケット処理・タイマー処理の前後で `net_lock()`/`net_unlock()` を呼ぶ。

別ミューテックスを増やさず `net_lock()` 一本で `wireguard_device`/`wireguard_peer` の状態と netdev の状態を両方保護している。`sendto()` を `net_lock()` 保持中に呼んでも、再帰的ミューテックスなのでデッドロックしない。

### タイマー処理は RX スレッドの受信タイムアウトに相乗り

参照実装は lwIP の `sys_timeout()` で 400ms ごとにハンドシェイク/keepalive を処理する。NuttX 版では別スレッドや `wd_start()` を新設する代わりに、RX スレッドの `recvfrom()` に `SO_RCVTIMEO=400ms` を設定し、タイムアウトのたびに同じ関数でタイマー処理 (`wg_run_timers()`) を呼ぶ構成にした。スレッドが1本で済み、ロック順序も単純になる。

### Cookie/mac2 経路は未実装(意図的)

`wireguard_is_under_load()` はプラットフォーム層で常に `false` を返す(CLAUDE.md / 参照実装のガイダンス通り)。そのため参照実装の mac2/cookie reply 経路は到達不能なデッドコードになる。`nuttx-wireguardif.c` ではこの経路を実装せず、コメントでその理由を明記した。将来 DoS 負荷判定を追加する場合はここに手を入れる。

---

## 実際の手順とエラー対応

### エラー: `CONFIG_NET_WIREGUARD` / `CONFIG_NET_TUN` が `make olddefconfig` で消える

Dockerfile で `kconfig-tweak --enable CONFIG_NET_WIREGUARD` していても、ビルドされたイメージの `.config` を見ると影も形もなかった。

**原因:** `NET_LL_TUN` を使うために `depends on NET_TUN` を追加したが、NuttX の `NET_TUN` 自体が `depends on ALLOW_BSD_COMPONENTS`(BSD ライセンスコンポーネントの利用許諾)を要求している。`wireguard-lwip` 本体も BSD-3-Clause なので同じ理由で `NET_WIREGUARD` にも同じ依存を追加した。この許諾なしに `kconfig-tweak --enable CONFIG_NET_TUN` しても `make olddefconfig` が依存未充足として黙って無効化する。

**対応:** Dockerfile と `Kconfig` の両方に `CONFIG_ALLOW_BSD_COMPONENTS` を追加。

---

### エラー: `CONFIG_DEV_RANDOM` が sim では有効化できない

CLAUDE.md に書かれていた `CONFIG_DEV_RANDOM=y` は sim では機能しない。`DEV_RANDOM`(ハードウェア TRNG 用 `/dev/random`)は `depends on ARCH_HAVE_RNG` だが、sim アーキテクチャはこれを select していない。Dockerfile で有効化を試みても `make olddefconfig` に黙って落とされていた。

**対応:** `CONFIG_DEV_URANDOM` + `CONFIG_DEV_URANDOM_XORSHIFT128`(ソフトウェア PRNG、`ARCH_HAVE_RNG` 不要)に切り替えた。`wireguard_random_bytes()` はこちらが対応する `/dev/urandom` を読む実装にした。

---

### エラー: `storage size of 'tv' isn't known`

`nuttx-wireguardif.c` で `struct timeval` を使うのに `<sys/time.h>` を include し忘れていた。単純な追加漏れ。

---

### エラー: `undefined reference to 'NXsetsockopt'`

`setsockopt(SO_RCVTIMEO)` を使ってリンクエラー。`CONFIG_NET_SOCKOPTS` が sim のデフォルト設定で無効だったため、`setsockopt()` の実体がビルドに含まれていなかった。

**対応:** `Kconfig` に `depends on NET_SOCKOPTS` を追加し、Dockerfile で有効化。

---

### 問題: `wg0` の MTU が 296 になる

ビルドは通ったが `ifconfig` の `wg0` の MTU が 296 だった。原因は `CONFIG_NET_TUN_PKTSIZE` のデフォルト値が 296(NuttX 全体の安全側デフォルト)で、`NET_LL_TUN` の `netdev_register()` がこれを `d_pktsize` としてそのまま使うため。WireGuard の実用的な MTU (1420) にはほど遠い。

**対応:** Dockerfile で `CONFIG_NET_TUN_PKTSIZE=1500` に設定。

---

### 問題: `wg0` と `eth0` の IP アドレスが衝突する

`CONFIG_NET_WIREGUARD_LOCAL_IPADDR` のデフォルトを何気なく `10.0.0.2` にしていたが、これは sim の `eth0` のデフォルトアドレスと同じだった。両方を同時に起動すると `ifconfig` 上で同一 IP の2つのインターフェースが並ぶ状態になる。

**対応:** デフォルトを `10.10.0.2` (peer 側 allowed-ip も `10.10.0.1`) に変更し、サブネットを分離した。

---

## Phase 2 完了時の状態

`sim:nsh` 上でテスト用の秘密鍵を設定し、`nsh> wg` を実行すると `wg0` が起動し `ifconfig` に表示されることを確認した:

```
nsh> wg
wg0 is up (listen port 51820)
nsh> ifconfig
eth0	Link encap:Ethernet HWaddr 00:00:00:00:00:00 at RUNNING mtu 576
	inet addr:10.0.0.2 DRaddr:10.0.0.1 Mask:255.255.255.0

wg0	Link encap:TUN at RUNNING mtu 1500
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0
```

- `nuttx-platform.c`: `clock_gettime()` / `/dev/urandom` / TAI64N / `wireguard_is_under_load()=false` を実装
- `nuttx-wireguardif.c`: `wg0` を `NET_LL_TUN` の netdev として登録。UDP ソケットを介した暗号化パケットの送受信、ハンドシェイク/keepalive タイマー、Kconfig からのピア設定を実装(`wireguardif.c` のロジックを NuttX API に置き換えて移植)
- `wg_main.c`: `wg` / `wg show` という NSH ビルトインコマンドを追加。`wg`(または `wg up`)は Kconfig の設定から `wg0` を起動、`wg show` はインターフェース公開鍵・ピアのエンドポイント・最終ハンドシェイク時刻・送受信バイト数を表示する(Phase 4 で前倒しして最小限を実装。`wg setconf` はまだ)
- `Kconfig`: 秘密鍵・リッスンポート・ピア公開鍵・エンドポイント・allowed-ip・トンネル IP アドレスを設定可能に

**未検証・未実装(Phase 3 以降):**
- 実際の WireGuard ハンドシェイク(Linux ピアとの疎通)は未確認。ロジックは `wireguardif.c` を忠実に移植したが、実際に鍵交換が成立するかは QEMU 上でのエンドツーエンド確認が必要
- QEMU (`qemu-armv7a`) 上での動作確認(Dockerfile 側の設定は sim と同様に追加済みだが、実機・実プロトコルでの検証はまだ)
- 実機 (ESP32-S3 / SPRESENSE) 上での検証は未着手

## Phase 3 への引き継ぎ事項

- Linux 側 `wg` コマンドとの実ハンドシェイクを QEMU 上で確認する
- `wg_run_timers()` / `wg_encrypt_and_send()` まわりのタイミング挙動(RTOS スケジューラ下での keepalive/rekey)を QEMU で検証する
- cookie reply 経路(`wireguard_is_under_load()`)を実装するかどうかの判断
