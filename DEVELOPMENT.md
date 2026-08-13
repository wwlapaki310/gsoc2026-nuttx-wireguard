# 開発ステータス

このリポジトリの「今どこまで進んでいるか」をまとめたもの。詳細な作業ログやこれからの計画は各ドキュメントにリンクしている。

**現在地を一言で言うと:** sim と QEMU(qemu-armv7a + TAP) の両方で、本物の Linux カーネル WireGuard ピアとの実ハンドシェイクとトンネル越し ping(0% packet loss)を確認済み。プロトコル・暗号実装としては実際に NuttX 上で動作することが実証できた。残る主なギャップは実機(ESP32-S3、未着手)。

---

## フェーズ別ステータス

[docs/proposal.ja.md](docs/proposal.ja.md) のロードマップに対する進捗:

| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 0 | 開発環境構築・移植スコープの把握 | ✅ 完了 |
| Phase 1 | ビルドシステム統合(`CONFIG_NET_WIREGUARD=y` でビルドが通る) | ✅ 完了 |
| Phase 2 | プラットフォーム層実装 + netif 統合(`wg0` が `ifconfig` に出る) | ✅ 完了(sim で確認済み) |
| Phase 3 | ハンドシェイクとトンネル疎通(Midterm) | ✅ sim と QEMU(qemu-armv7a + TAP) の両方で本物の Linux WireGuard ピアとの実ハンドシェイク・ping 疎通を確認 |
| Phase 4 | NSH コマンド・Kconfig 統合・実機テスト | 🟡 一部前倒し実装済み(`wg` / `wg show`)。実機検証は未着手 |
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

## まだ確認できていないこと

- **長時間・異常系の通信**: sim/QEMU の短時間 handshake + ping は確認済みだが、長時間 keepalive、再接続、MTU 境界、複数 peer は未確認
- **実機(ESP32-S3)**: 手順書([docs/hardware-verification.md](docs/hardware-verification.md))は書いたが、実機での書き込み・Wi-Fi 接続・トンネル確認はまだ何も実施していない。そもそも Dockerfile に ESP32-S3 用(Xtensa)ツールチェインが入っていない
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
| [docs/hardware-verification.md](docs/hardware-verification.md) | ESP32-S3 実機検証の手順書(未実施) |
| [docs/upstream-strategy.md](docs/upstream-strategy.md) | apache/nuttx-apps へのマージ戦略 |

---

## 次にやること(優先順)

1. ESP32-S3 実機検証([docs/hardware-verification.md](docs/hardware-verification.md) の実施)
2. 長時間 keepalive、再接続、MTU 境界、複数 peer の追加検証
3. upstream 提出に向けたコーディングスタイル整形・`LICENSE` 追記([docs/upstream-strategy.md](docs/upstream-strategy.md) の「次にやること」)
