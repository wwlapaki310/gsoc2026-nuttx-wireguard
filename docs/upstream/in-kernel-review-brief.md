# In-kernel WireGuard for NuttX — レビューブリーフ

このファイルは **単体で読める** ように書いてあります。他の LLM やレビュアーに
このファイル全体を貼って、設計・実装のレビューをもらうための資料です。
リポジトリや過去の文脈を知らない前提で書いています。

- 最終更新: 2026-09-20
- 正本(この資料の元): 計画 [in-kernel-plan.md](in-kernel-plan.md) / 状況 [in-kernel-status.md](in-kernel-status.md)
- レビュー往復の記録は GitHub Issue（このブリーフのリンクを貼って運用）

レビューしてほしいことは末尾の「## レビュー論点」に番号付きでまとめています。
そこだけ答えてもらっても構いません。

---

## 1. 何をするものか

Apache NuttX（独自 TCP/IP スタックを持つ RTOS。lwIP ではない）に **WireGuard VPN を
仮想ネットワークデバイス `wg0` として実装** する。`wg0` に routing された IP パケットは
暗号化されて UDP でピアに送られ、listen ポートに来た UDP は復号されて `wg0` から
受信したかのようにスタックへ渡る。ピアは任意の WireGuard 実装（Linux カーネル、
wireguard-go、boringtun、別の NuttX）でよい。

既に FLAT ビルド専用の apps 実装（別プロジェクトで v0.1.1、実機動作）があり、本作業は
それを **カーネル内 netdev + ユーザー空間 ioctl クライアント** に作り直して upstream に
出せる形にするもの。

### 目的 / 非目的

- 目的: (a) FLAT/PROTECTED/KERNEL いずれのビルドでも動く netdev として実装、(b) 鍵が
  カーネル外に漏れない ABI、(c) upstream にマージしやすい粒度と検証。
- 非目的（今回）: IPv6、動的ピア数、Netlink 風の設定 API、複数 wg デバイス間ルーティング。

---

## 2. 全体構成

```
ユーザー空間                          カーネル
-----------                          --------
apps/system/wg  --ioctl(AF_INET)-->  net/netdev/netdev_ioctl.c
  (wg コマンド)                         └─ dev->d_ioctl ─> drivers/net/wireguard/
                                                            wireguard.c   (netdev_lowerhalf)
                                                            wg_noise.c    (Noise_IKpsk2 本体)
                                                            wg_crypto.c   (NuttX crypto/ の薄い層)
                                                              │
                                              UDP socket ─────┘  ← RX kthread が poll/recv/復号
```

- `wg0` は `NET_LL_TUN` リンク種別（L2 ヘッダなし、素の IP を運ぶ）。
- ドライバはカーネル内で `struct socket`（UDP）を1本保持し、専用 RX kthread が
  `psock_poll`→受信→復号→`ip_input` でスタックへ渡す。TX は `netdev_lowerhalf` の
  transmit から暗号化して UDP 送信。先例: `net/rpmsg/rpmsgdrv.c`（カーネルが socket +
  kthread を持つドライバ）。
- 暗号は NuttX 純正 `crypto/`（BLAKE2s / ChaCha20-Poly1305 / Curve25519）。プロトコル本体
  （ハンドシェイク状態機械・鍵導出・アンチリプレイ窓・負荷時 cookie）は
  wireguard-lwip（BSD-3-Clause）由来を NuttX スタイルに移植。

---

## 3. 主要な設計判断（レビュー対象）

1. **netdev_lowerhalf + カーネル UDP socket + RX kthread**。lwIP netif ではなく NuttX の
   netdev フレームワークに載せる。RX を割り込みでなく kthread の socket poll にした。
2. **フラット固定長・ポインタなしの ioctl ABI**（`include/nuttx/net/wireguard.h`）。理由:
   PROTECTED/KERNEL ビルドではカーネルが呼び出し側の構造体を直接読み書きするため、
   構造体内にポインタがあると検証できない。可変長（allowed-ips 等）は上限固定の配列。
3. **秘密鍵は write-only**。`SIOCSWGIF` は受け取るが `SIOCGWGIF` は**返さない**(公開鍵は
   導出して返す)。ただし秘密鍵は設定ファイル(ユーザー側、下の point 6 の正本)にも存在するため、
   「鍵素材がカーネル外に一切出ない」わけではない。正確には**get ioctl が秘密鍵を返さない**。
4. **`net_ioctl_arglen()` には登録しない**。usrsock デーモンに横取りされないよう、
   WireGuard ioctl は `netdev_ioctl.c` から直接ディスパッチする。
5. **crypto は NuttX 純正を使う**（vendored しない）。ただし `wg` コマンドのオフライン
   genkey/pubkey 用に MIT ライセンスの X25519 を1ファイルだけ同梱（`wg_x25519.c`）。
6. **秘密鍵の正本は設定ファイル**。ドライバが鍵を返さないので、`wg` コマンドは
   `wg(8)` 形式の設定ファイルを正本にし、`set`/`setconf` でデバイスへ push、
   `saveconf` はファイル側から書く。
7. **アンチリプレイ窓 2048**（Linux は 8192、参照実装は 32）。Wi-Fi の並べ替え耐性。

---

## 4. ioctl ABI（要点）

`AF_INET` ソケットに対する5コマンド（`include/nuttx/net/ioctl.h` の 0x0046..0x004A）:

- `SIOCSWGIF` / `SIOCGWGIF`（`struct wg_ifreq_s`）: private key（write-only）/ listen port /
  トンネルアドレス / up-down。get は導出した public key を返す。
- `SIOCSWGPEER` / `SIOCDWGPEER` / `SIOCGWGPEER`（`struct wg_peerreq_s`）: ピアの追加更新 /
  削除 / 列挙。set/delete は public key で指定、get は index で列挙し endpoint・
  allowed-ips・keepalive・統計（最終ハンドシェイク・rx/tx バイト）を返す。

秘密鍵と listen port と `setconf` は `wg0` が down のときだけ受け付ける。

---

## 5. 検証状況（3段 + 発見した2バグ）

1. **CI コンパイル経路**: `boards/sim/sim/sim/configs/wireguard/defconfig`。
   `./tools/configure.sh sim:wireguard` でクリーンビルド。
2. **sim ランタイム（実 Linux カーネル WireGuard 相手）**: 双方向ハンドシェイク・
   トンネル ping・`saveconf`/`setconf` 往復・on-device genkey・pubkey が wg(8) と一致、
   すべて PASS。否定系（リプレイで endpoint が動かない / 洪水に cookie 応答）も PASS。
3. **実カーネルビルドの完全トンネル**: `rv-virt:knetnsh64`（BUILD_KERNEL + virtio-net）を
   QEMU で起動し、host TAP 上の実 Linux WireGuard と双方向トンネル + ping。`wg` は hostfs
   越しに **別 ELF** としてロードされ、driver は **syscall 境界越し**に叩かれる。
   qemu-armv7a:knsh の BUILD_KERNEL ビルドも通る（apps/kernel シンボル分離の証拠）。

### 発見・修正したバグ（2件。内訳: **既存 NuttX 本体が1件、新規ドライバが1件**）

- **[既存 NuttX 本体] `crypto/chachapoly.c` の u64 nonce 配置**: counter を nonce の bytes 0..7 に置いていたが
  RFC 8439 / WireGuard は bytes 4..11。counter 0 は一致するのでハンドシェイクは通るが
  データ2個目以降が全滅。in-tree に利用者がおらず未検出だった。→ `memcpy(...+4, ...)`。
  これは **WireGuard driver PR とは別の `crypto:` PR** として先に出す。
- **[新規ドライバ] BUILD_KERNEL スタックオーバーフロー**: `wg_set_if()` が鍵変更時に
  `struct wg_peer_s saved[WG_MAX_PEERS]`（`sizeof=1512`、4ピアで 6048B）を **スタックに**
  確保。BUILD_KERNEL の kernel stack は 3072B で溢れてヒープ破損 → panic。sim（FLAT、
  大きいタスクスタック）では露見せず。→ `kmm_malloc`/`kmm_free` でヒープへ。

---

## 6. 変更規模

nuttx（fork `net-wireguard`、base upstream/master `c95c546c`、機能単位2コミット）:

```
drivers/net/wireguard/wireguard.c        1666   netdev_lowerhalf, UDP socket, RX kthread, timers, cookie, ioctl
drivers/net/wireguard/wg_noise.c         1170   Noise_IKpsk2 本体、replay 窓、負荷時 cookie
drivers/net/wireguard/wg_crypto.{c,h}     585   NuttX crypto/ の薄い層
include/nuttx/net/wireguard.h             169   ioctl ABI（この1枚がユーザー/カーネル共有）
net/Kconfig                                94   NET_WIREGUARD オプション群
net/netdev/netdev_ioctl.c                  74   ioctl ディスパッチ
crypto/chachapoly.c                        16   nonce バグ修正（別 crypto: PR にする分）
（他に defconfig / Documentation×2 / 統合点少々）        計 +4470 行
```

apps（fork `system-wg`、1コミット）:

```
system/wg/wg_main.c      1286   ioctl クライアント（up/down/show/showconf/setconf/saveconf/set/genkey/pubkey）
system/wg/wg_x25519.{c,h} 619   MIT ライセンスの X25519（オフライン genkey/pubkey 用、同梱）
                                 計 +2042 行
```

diff 全体が要る場合の渡し方: 各 fork で
`git diff <merge-base with upstream/master>..HEAD`（このブリーフに添付するか、
必要ファイルだけ抜粋して貼る）。

---

## 7. レビュー論点（ここに答えてほしい）

1. **ABI の形**: フラット固定長 ioctl は妥当か。将来 IPv6 やピア数増を見据えると
   Netlink 風にすべきか、それとも固定長のまま拡張フラグで足すのが NuttX 流か。
2. **RX を kthread の socket poll にした**設計は妥当か。割り込み駆動や既存の
   `netdev_lowerhalf` の RX 経路に寄せる余地は。ストール/優先度逆転のリスク。
3. **カーネルが UDP socket を保持**する形（rpmsgdrv 先例）で、ライフサイクル
   （ifdown 時の socket/kthread 停止・再 up）に穴はないか。
4. **秘密鍵 write-only + 設定ファイル正本**のモデルは受け入れられるか。鍵を返さない
   ことで運用（バックアップ・移行）に問題は出ないか。
5. **`wg_x25519.c`（MIT 同梱）の是非**。NuttX 純正 `CRYPTO_CURVE25519` をユーザー空間から
   使えるなら同梱を消せる。offline genkey/pubkey のために同梱する判断は妥当か。
6. **crypto nonce 修正を別 PR** にする順序・粒度。KAT（`crypto/testmngr.c`）を足すべきか。
   `xchacha20poly1305`（cookie 用、24B nonce）も同種の確認が要るか。
7. **アンチリプレイ窓 2048** や `MAX_PEERS`/`MAX_AIPS` の既定値・上限は妥当か。静的確保の
   メモリ（1ピア ≈ 1.5KB + replay 窓×3）とのトレードオフ。
8. **検証の十分性**: sim + rv-virt:knetnsh64 + 実機（予定）で upstream レビューに足りるか。
   追加で欲しい負荷/異常系テストは。
9. **コミット分割**: (a) crypto: nonce、(b) net/wireguard 一式、(c) apps/system/wg の3本立ては
   適切か。driver 一式（+4470行）をさらに割るべきか。
10. **見落としているスレッド安全性 / エンディアン / メモリ安全性**の箇所。
