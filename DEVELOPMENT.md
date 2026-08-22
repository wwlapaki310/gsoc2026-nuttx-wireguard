# 開発ステータス

このリポジトリの「今どこまで進んでいるか」をまとめたもの。詳細な作業ログやこれからの計画は各ドキュメントにリンクしている。

**現在地を一言で言うと:** **ESP32-S3 実機で、実 Wi-Fi・実ピア(Windows 公式 WireGuard クライアント)との実ハンドシェイクとトンネル越し ping(0% packet loss)、さらにトンネル越し telnet でのコマンド実行・Web サーバーアクセスまで確認した。** sim・QEMU の仮想ネットワークだけでなく、本物のシリコン・本物のネットワーク環境でも WireGuard 実装が正しく動作することを実証できた(プロポーザル Phase 4 の目標を達成)。その過程で「TCP のアプリケーションデータだけがトンネルを通らない」バグ(LPWORK ワーカースレッドからの `sendto()` が `EBADF` で失敗していた)を発見し、`psock_*()` 内部 API への切り替えで修正済み。ESP32-WROOM-32 と Sony Spresense 向けにもコード変更なしでビルドが通ることを確認したが、こちらは実機への書き込み・起動確認がハードウェア側の問題(詳細は [docs/phase4-log.md](docs/phase4-log.md))で止まっている。

**デモ動画:** [https://youtu.be/1kyX2av5WG4](https://youtu.be/1kyX2av5WG4) — telnet ログイン→コマンド実行→Web サーバー起動→ブラウザアクセスまでの一連の流れ。まとめは [docs/phase4-summary.md](docs/phase4-summary.md)。

---

## フェーズ別ステータス

[docs/proposal.ja.md](docs/proposal.ja.md) のロードマップに対する進捗:

| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 0 | 開発環境構築・移植スコープの把握 | ✅ 完了 |
| Phase 1 | ビルドシステム統合(`CONFIG_NET_WIREGUARD=y` でビルドが通る) | ✅ 完了 |
| Phase 2 | プラットフォーム層実装 + netif 統合(`wg0` が `ifconfig` に出る) | ✅ 完了(sim で確認済み) |
| Phase 3 | ハンドシェイクとトンネル疎通(Midterm) | ✅ sim と QEMU(qemu-armv7a + TAP) の両方で本物の Linux WireGuard ピアとの実ハンドシェイク・ping 疎通を確認 |
| Phase 4 | NSH コマンド・Kconfig 統合・実機テスト | ✅ `wg` / `wg show` 実装済み。**ESP32-S3 実機で実 Wi-Fi・実ピアとの WireGuard ハンドシェイク・トンネル ping を確認**(0% packet loss)。**トンネル越し telnet で見つかった TCP 特有バグ(`EBADF`)を修正し、コマンド実行まで確認**。ESP32-WROOM-32・Spresense はビルド成功もハードウェア側の問題で書き込み未達成 |
| Phase 5 | upstream PR | ⛔ 未着手。方針は [docs/upstream-strategy.md](docs/upstream-strategy.md) にまとめ済み |

---

## 実装済みのもの

`nuttx_port/apps/netutils/wireguard/` 以下:

| ファイル | 内容 |
|---|---|
| `nuttx-platform.c` | `wireguard-platform.h` の4関数を実装(`clock_gettime`・`/dev/urandom`・TAI64N・`is_under_load()=false`) |
| `nuttx-wireguardif.c` | `wg0` を NuttX の `NET_LL_TUN` netdev として登録。UDP ソケットを「配線」に見立てて暗号化パケットの送受信・ハンドシェイク/keepalive タイマーを実装(参照実装 `wireguardif.c` の移植) |
| `nuttx-wireguardif.h` | 上記の公開 API(`wg_initialize()` / `wg_show()`) |
| `wg_main.c` | NSH ビルトインコマンド `wg`(起動)/ `wg show`(公開鍵・ピア状態・送受信バイト数の表示) |
| `Kconfig` | 秘密鍵・リッスンポート・ピア公開鍵/エンドポイント/allowed-ip・トンネル IP を設定可能に |
| `Makefile` / `CMakeLists.txt` | 上記を NuttX のビルドシステムに統合(builtin `wg` コマンド登録込み) |

`Dockerfile` はビルドが通るよう複数の Kconfig 依存関係の問題を修正済み(`ALLOW_BSD_COMPONENTS`・`NET_SOCKOPTS`・`/dev/urandom` 周りなど。詳細は [docs/phase2-log.md](docs/phase2-log.md))。

---

## 動作確認できていること

### 実ハンドシェイク・トンネル疎通(sim + 本物の Linux WireGuard ピア)

sim コンテナ内で、本物の Linux カーネル WireGuard(`ip link add wg0 type wireguard` — `wireguard-go` ではなく実カーネルモジュール)をピアとして動かし、実際にハンドシェイクとトンネル疎通を確認した:

```
# NuttX 側 wg show
peer: oE7iG2E+ks6qA4pOegvXb8R9Jax1skHzdpUKyOdoxy0=
  endpoint: 10.0.0.1:51821
  latest handshake: 7 seconds ago
  transfer: 352 B received, 252 B sent

# Linux 側から ping 10.10.0.2 (NuttX の wg0 アドレス)
3 packets transmitted, 3 received, 0% packet loss
```

詳細は [docs/phase3-log.md](docs/phase3-log.md)。この過程で実装上の2つのバグ(`SO_RCVTIMEO` が効かない、detached pthread が生成元タスクの終了とともに動かなくなる)を発見・修正済み。

再実行コマンド:

```bash
docker run --rm --cap-add=NET_ADMIN --device=/dev/net/tun \
  -v ${PWD}/scripts:/workspace/scripts:ro \
  --entrypoint bash nuttx-wireguard:sim \
  /workspace/scripts/verify-sim-wireguard.sh
```

### 実ハンドシェイク・トンネル疎通(QEMU qemu-armv7a + TAP + Linux WireGuard ピア)

QEMU でも NuttX 自身のスケジューラ上で `eth0` と `wg0` を起動し、同じ Docker コンテナ内の Linux カーネル WireGuard ピアとハンドシェイク・トンネル疎通を確認した。QEMU の `hostfwd` 方式では UDP 51820 が guest に届かず handshake が成立しなかったため、検証スクリプトでは Docker コンテナ内に `tapqemu` を作成し、QEMU guest と Linux peer を同じ L2 ネットワークに置いている。

```
# QEMU NuttX 側 wg show
interface: wg0
  listening port: 51820
peer: LO9bnmXH0WXGm8CXFj41rV8+vdCYlrt1ckll6TS4bUQ=
  endpoint: 10.0.0.1:51821
  latest handshake: 9 seconds ago
  transfer: 352 B received, 252 B sent

# Linux 側から ping 10.10.0.2 (QEMU NuttX の wg0 アドレス)
3 packets transmitted, 3 received, 0% packet loss
```

再実行コマンド:

```bash
docker run --rm --cap-add=NET_ADMIN --device=/dev/net/tun \
  -v ${PWD}/scripts:/workspace/scripts:ro \
  --entrypoint bash nuttx-wireguard:qemu \
  /workspace/scripts/verify-qemu-wireguard.sh
```

### `wg0` の netdev 登録(Phase 2 時点)

```
nsh> wg
wg0 is up (listen port 51820)
nsh> ifconfig
wg0	Link encap:TUN at RUNNING mtu 1500
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0
```

- QEMU (`qemu-armv7a`) は `Dockerfile` の VirtIO/FDT 設定を修正し、`eth0` 表示・`wg0` 起動・Linux peer との handshake/ping まで確認済み(詳細は [docs/phase3-log.md](docs/phase3-log.md))

### 実機での実ハンドシェイク・トンネル疎通(ESP32-S3 + 実 Wi-Fi + Windows 公式 WireGuard クライアント)

ESP32-S3 実機を実際の家庭用 Wi-Fi アクセスポイントに接続し、Windows 上の公式 WireGuard クライアント(`winget install WireGuard.WireGuard`、Linux カーネル実装ではない)をピアとしてハンドシェイクを成立させた:

```
# ESP32-S3 側 wg show
nsh> ifconfig
wlan0	inet addr:192.168.0.152 DRaddr:192.168.0.1 Mask:255.255.255.0   # 実ルーターから DHCP 取得

nsh> wg show
peer: 5J5rgkz5RB0CB1hIZae5V3jQjisRjqOrry7Scca9YjE=
  endpoint: 192.168.0.216:51820
  latest handshake: 11 seconds ago
  transfer: 336 B received, 240 B sent
```

```
# Windows 側から
> ping 10.10.0.2
Packets: Sent = 4, Received = 4, Lost = 0 (0% loss)
```

sim・QEMU の仮想ネットワークだけでなく、本物のシリコン・本物の Wi-Fi・本物の異実装ピア(Windows 公式クライアント)との相互運用性まで実証できた。詳細な手順・ログは [docs/phase4-log.md](docs/phase4-log.md)。

### トンネル越し telnet でのコマンド実行(TCP 特有バグ修正後)

ping(ICMP)は動くのに telnetd(TCP)だけデータが一切届かないバグを発見・修正した(原因: LPWORK ワーカースレッドから `sendto()` する際に fd が `EBADF` になっていた。`psock_*()` 内部 API に切り替えて解決。詳細は [docs/phase4-log.md](docs/phase4-log.md) の該当節)。修正後、トンネル越しの telnet セッションでコマンドを実行し、結果が正しく返ってくることを確認した:

```
==== telnet demo (10.10.0.2:23 経由) ====
BANNER:
NuttShell (NSH) NuttX-12.7.0
nsh>
---- uname -a ----
NuttX  12.7.0 5d8cdeae-dirty Aug 16 2026 22:49:33 xtensa esp32s3-devkit
---- uptime ----
00:01:38 up  0:01, load average: 0.00, 0.00, 0.00
---- free ----
                 total       used       free    maxused    maxfree  nused  nfree
      Umem:     291648      93168     198480      94440     198432    201      2
```

### 実機向けクロスビルド(ESP32-WROOM-32 / ESP32-S3 / Sony Spresense)

`Dockerfile` に `esp32`・`esp32s3`・`spresense` の3ステージを追加し、いずれも **`nuttx-platform.c`/`nuttx-wireguardif.c`/`wg_main.c` にコード変更を一切加えずに** `CONFIG_NET_WIREGUARD=y` でのビルドが成功することを確認した:

```bash
docker build --target esp32 -t nuttx-wireguard:esp32 .         # nuttx.bin が生成される (ESP32-WROOM-32)
docker build --target esp32s3 -t nuttx-wireguard:esp32s3 .     # nuttx.bin が生成される (ESP32-S3)
docker build --target spresense -t nuttx-wireguard:spresense . # nuttx.spk が生成される
```

無印 ESP32・Spresense で足止めされているブートモード/USB認識問題は ESP32-S3(別個体)では発生しなかった — チップ世代というより個体/ボード側の問題だった可能性が高い。

**余談:** Raspberry Pi Pico W / Pico 2 W も検討したが、NuttX にオンボード Wi-Fi チップ(CYW43439)用のドライバが存在しないため、現状 Wi-Fi 経由の WireGuard 通信はできないことが判明した(詳細は [docs/hardware-verification.md](docs/hardware-verification.md) のスコープ節)。

sim/QEMU 向けに書いたプラットフォーム層・netif 統合コードがそのまま Xtensa (ESP32/ESP32-S3) と ARM Cortex-M4F (Spresense) の両方でポータブルに動くことの実証になった。

## まだ確認できていないこと

- **長時間・異常系の通信**: sim/QEMU/ESP32-S3 いずれも短時間の handshake + ping + telnet(TCP)は確認済みだが、長時間 keepalive、再接続、MTU 境界、複数 peer は未確認
- **ESP32-WROOM-32・Spresense の実機起動確認**: ビルドは成功するが、実機への書き込みで足止めされている。ESP32 はブートモードに入らない(`Wrong boot mode detected`)、Spresense は USB デバイスとして列挙されない。どちらもハードウェア側の問題を疑っているが未特定。詳細と試行錯誤の記録は [docs/phase4-log.md](docs/phase4-log.md)、手順書は [docs/hardware-verification.md](docs/hardware-verification.md)
- **upstream 提出に向けた整形**: コーディングスタイル(`checkpatch.sh`/`nxstyle`)準拠、`LICENSE` 追記、コミットの `Assisted-by:` タグ運用は未着手(詳細は [docs/upstream-strategy.md](docs/upstream-strategy.md))

---

## 今すぐ試す方法

```bash
docker build --target sim -t nuttx-wireguard:sim .
docker run --rm --cap-add=NET_ADMIN --device=/dev/net/tun \
  -v ${PWD}/scripts:/workspace/scripts:ro \
  --entrypoint bash nuttx-wireguard:sim \
  /workspace/scripts/verify-sim-wireguard.sh

docker build --target qemu -t nuttx-wireguard:qemu .
docker run --rm --cap-add=NET_ADMIN --device=/dev/net/tun \
  -v ${PWD}/scripts:/workspace/scripts:ro \
  --entrypoint bash nuttx-wireguard:qemu \
  /workspace/scripts/verify-qemu-wireguard.sh
```

`CONFIG_NET_WIREGUARD_PRIVATE_KEY` のデフォルトは空(セキュアなデフォルト)なので、手動起動で `wg` コマンドを見る場合は秘密鍵を設定してリビルドする必要がある。上記の検証スクリプトはテスト用鍵を一時生成してリビルドする。

---

## ドキュメント一覧

| ドキュメント | 内容 |
|---|---|
| [docs/proposal.ja.md](docs/proposal.ja.md) | GSoC プロポーザル本体・全体ロードマップ |
| [docs/dev-environment.md](docs/dev-environment.md) | sim / QEMU / 実機の開発環境の使い分け |
| [docs/phase1-log.md](docs/phase1-log.md) | Phase 1(ビルドシステム統合)の作業ログ |
| [docs/phase2-log.md](docs/phase2-log.md) | Phase 2(プラットフォーム層・netif 統合)の作業ログ・詰まった点 |
| [docs/phase3-log.md](docs/phase3-log.md) | Phase 3(実ハンドシェイク検証)の作業ログ・見つかったバグと修正 |
| [docs/phase4-log.md](docs/phase4-log.md) | Phase 4(ESP32-S3 実機での実ハンドシェイク成功、TCP バグの調査・修正、ESP32-WROOM-32 / Spresense 書き込み試行)の作業ログ |
| [docs/phase4-summary.md](docs/phase4-summary.md) | Phase 4 の成果まとめ・デモ動画リンク |
| [docs/code-review-2026-08.md](docs/code-review-2026-08.md) | コード全体のレビュー結果・課題の棚卸し・今後の計画 |
| [docs/presentation-script.md](docs/presentation-script.md) | 発表原稿(スライド構成 + 話す内容 + 想定質問) |
| [docs/license-appendix-draft.md](docs/license-appendix-draft.md) | upstream 提出用の `LICENSE` 追記案 |
| [docs/hardware-verification.md](docs/hardware-verification.md) | 実機検証の手順書(ESP32-S3 は確認済み、ESP32-WROOM-32 / Spresense は書き込み以降未確認) |
| [docs/upstream-strategy.md](docs/upstream-strategy.md) | apache/nuttx-apps へのマージ戦略 |

---

## 次にやること(優先順)

全体像と根拠は [docs/code-review-2026-08.md](docs/code-review-2026-08.md) にまとめてある。

1. **dev@nuttx.apache.org での設計共有** — スタイル整形と実機ログが揃い、upstream に出す前提が埋まったので、次は設計そのものへの合意取り(`wg0` を lwIP netif ではなく NuttX netdev として実装した判断について)
2. **実行時設定 (`wg setconf` 相当)** — 鍵をビルドに焼き込まない。実用性・セキュリティ両面で最大の弱点
3. vendored ツリーの整理(未使用の `wireguardif.c` / `crypto/cortex/` の扱いを決め、Docker の `git clone` 方式から実ファイル同梱へ)
4. 長時間 keepalive、再接続、MTU 境界、複数 peer の追加検証(ESP32-S3 実機で)
5. ESP32-WROOM-32・Spresense の実機書き込み問題の切り分け(別 PC・別ケーブル/電源での再挑戦。[docs/phase4-log.md](docs/phase4-log.md) 参照)
