# WireGuard の Apache NuttX への移植

[Apache NuttX](https://nuttx.apache.org/) 上で動く WireGuard VPN の実装。`wg0` という
ネットワークデバイスとして見える。実機で、本物の WireGuard ピアを相手に検証済み。

> **議論:** [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548)
> **デモ:** [youtu.be/1kyX2av5WG4](https://youtu.be/1kyX2av5WG4) — telnet と Web サーバ、どちらもトンネル越し

---

## プロジェクト概要

WireGuard は Linux 向けに開発された軽量な VPN プロトコルで、組み込み・IoT 分野でも採用が
広がっている。UDP 上で暗号化トンネルを確立し、使用する暗号アルゴリズムは Curve25519・
ChaCha20-Poly1305・BLAKE2s。実装がコンパクト（約 4,000 行）で、マイコン上での動作にも
適している。

Apache NuttX は POSIX 準拠の RTOS で、独自の TCP/IP スタックと BSD ソケット API を持つ。
しかし現時点では VPN 機能が存在しない。本プロジェクトでは WireGuard を **NuttX の
ネットワークデバイス（`wg0`）** として実装し、NuttX 機器への安全な遠隔アクセスを可能にする。
参照実装として [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) を使用している。

### なぜこのプロジェクトが必要か

NuttX デバイスへの遠隔・安全なアクセスは、多くの分野で未解決の課題となっている。

- **エッジ AI・産業 IoT** — フィールドに展開されたデバイスに対して、物理的なアクセスなしにファームウェア更新や遠隔診断を行う必要がある
- **衛星・宇宙機** — NuttX は小型衛星プロジェクトで採用されている。打ち上げ後はネットワーク経由が唯一のメンテナンス手段になる
- **無人インフラ** — 海洋ブイ・山岳観測所・パイプラインなど遠隔地のセンサーには安全な双方向通信が必要
- **デバイス間直接通信** — クラウド中継なしに NuttX デバイス同士が暗号化トンネルで直接通信できる

VPN がない場合の現実的な選択肢は、グローバル IP を晒すか、独自プロトコルを作り込むか、
ベンダーのクラウドに乗るか——どれも嬉しくない。WireGuard のコンパクトな実装とシンプルな
鍵モデルは、こうした制約のある環境に適しており、しかも相手は既存の WireGuard
エンドポイントで構わない。

---

## 現状

トンネルは実機でエンドツーエンドに動き、実行時に設定でき、電源を落としても設定が残る。
残っているのは upstream への提出。

| | |
|---|---|
| **インターフェース** | `wg0`（UDP ソケットを配線とする `NET_LL_TUN` netdev） |
| **設定** | 実行時（`wg genkey` / `wg set` / `wg setconf`）。`wg(8)` 互換の INI 形式で永続化 |
| **ピア数** | 1〜16（Kconfig。1 ピアあたり `.bss` 約 904 バイト） |
| **スループット** | ESP32-S3・Wi-Fi 経由、トンネル越し TCP で 260 KiB/s |
| **最長連続動作** | 4 時間 28 分 |
| **upstream** | 未提出 — [残作業](#残作業)を参照 |

### 検証済みの環境

| 環境 | アーキテクチャ | 相手のピア | 確認したこと |
|---|---|---|---|
| sim | x86_64 / Linux | Linux カーネル WireGuard | ハンドシェイク・疎通・ランタイム設定・**2 ピア同時セッション**（スクリプト化済み） |
| QEMU | ARM Cortex-A7 | Linux カーネル WireGuard | NuttX 自身のスケジューラ上での疎通 |
| **ESP32-S3** | Xtensa LX7 | **Windows 公式クライアント** | **実 Wi-Fi 越し**の telnet・HTTP・7 MB 転送・rekey・電源断からの復帰 |
| Spresense | ARM Cortex-M4F | — | `wg0` の起動（**コード変更ゼロ**） |

通信相手は常に本物の WireGuard 実装（Linux カーネルモジュールと Windows 公式クライアント）。
自作同士で通信しても相互運用性の証明にならないため。

NuttX 12.7.0 と `master` の両方で、コード変更なしにビルドが通る。

---

## アーキテクチャ

```
+-----------------------------------------------+
|                  NuttX RTOS                   |
|                                               |
|  アプリケーション / NSH                        |
|           |                                   |
|    NuttX ネットワークスタック (BSD socket API) |
|       |              |                        |
|  eth0 / wlan0       wg0  <- 本プロジェクト     |
|  (物理 NIC)      (WireGuard netdev)           |
|                      |                        |
|         +------------+------------+           |
|         |            |            |           |
|   wireguard.c   nuttx-wireguardif.c   nuttx-  |
|   + crypto/     (netdev + UDP ソケット) platform.c
|   無改変        NuttX 向けに新規作成   (時刻・  |
|                      |                 乱数)   |
|              UDP ソケット (ポート 51820)       |
+-----------------------------------------------+
              |
    インターネット / LTE / 衛星回線
              |
    WireGuard ピア (Linux, Windows, ...)
```

**NuttX は lwIP ではなく独自の TCP/IP スタックを持つ**ため、参照実装の netif グルーは
そのまま使えない。`wg0` は `drivers/net/tun.c` を手本に `netdev_register()` で登録している。
プロトコルと暗号のソースは無改変で持ち込んでいる。

送信は `devif_poll()` → 暗号化 → `psock_sendto()`。受信はバックグラウンドタスクが
`psock_poll()` でブロックし、復号して `ipv4_input()` で注入する。

> ソケットはファイルディスクリプタではなく `struct socket` として保持している。NuttX は fd を
> タスクグループ単位でスコープするが、送信経路は無関係なワーカースレッド上で走るため。
> この都合で現状の実装は FLAT ビルド前提になっている —
> [Issue #6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6)。

---

## 使い方

```
nsh> wg genkey
<base64 の秘密鍵>
nsh> wg set private-key <KEY>
nsh> wg set peer <PUBKEY> endpoint 203.0.113.9:51820 \
                          allowed-ips 10.10.0.1/32 \
                          persistent-keepalive 25
nsh> wg up
nsh> wg show
interface: wg0
  public key: <base64>
  listening port: 51820
peer: <base64>
  endpoint: 203.0.113.9:51820
  latest handshake: 5 seconds ago
  transfer: 1.23 KiB received, 0.45 KiB sent

nsh> wg saveconf          # -> /data/wg0.conf、次回起動時に自動で読まれる
```

設定ファイルは `wg(8)` と同じ INI 形式（`[Interface]` / `[Peer]`）なので、デスクトップの
WireGuard の設定をそのまま持ち込める。

> ランタイム設定を使うなら `CONFIG_LINE_MAX`（12.7.0 では `CONFIG_NSH_LINELEN`）を 160 以上に
> すること。`wg set peer` の行は約 134 文字あり、デフォルトでは NSH が**無言で切り詰める**。

---

## 開発環境

Docker ベース。ビルドターゲットは3つ。

```bash
# sim — メイン開発環境（高速なビルド・テストループ）
docker build --target sim -t nuttx-wireguard:sim .
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wireguard:sim

# QEMU — RTOS 挙動の検証
docker build --target qemu -t nuttx-wireguard:qemu .
docker run --rm -it nuttx-wireguard:qemu

# ESP32-S3 — 実機
docker build --target esp32s3 -t nuttx-wireguard:esp32s3 .
```

`--build-arg NUTTX_REF=<ref>` で NuttX のリビジョンを切り替えられる（既定は `nuttx-12.7.0`。
`master` でもビルドが通ることを確認済み）。

詳細は [docs/development/dev-environment.md](docs/development/dev-environment.md) と [DEVELOPMENT.md](DEVELOPMENT.md)。

### 検証スクリプト

| スクリプト | 何を証明するか |
|---|---|
| `scripts/verify-sim-wg-runtime.sh` | 実行時だけで設定したトンネルが実際の Linux ピアに届き、保存・復元を経ても動き、不正な入力を拒否すること |
| `scripts/verify-sim-wg-multipeer.sh` | 2 つの Linux WireGuard インターフェースが `wg0` と同時にセッションを保持すること |

---

## ソース構成

すべて [`nuttx_port/apps/netutils/wireguard/`](nuttx_port/apps/netutils/wireguard) の下にあり、
`apache/nuttx-apps` にそのまま提出できる形に置いてある。

| | 行数 | 由来 |
|---|---:|---|
| `wireguard.c`, `crypto/` | 3,079 | [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip)（BSD-3-Clause）**upstream とバイト一致** |
| `nuttx-wireguardif.c` | 2,157 | NuttX 向けに新規作成 |
| `wg_main.c` | 344 | `wg` NSH コマンド |
| `nuttx-wireguardif.h` | 266 | |
| `nuttx-platform.c` | 186 | OS 依存の 4 関数 |
| Kconfig・ビルド統合 | 248 | |

参照実装がプラットフォームに要求するのは、この 4 関数だけ:

| 関数 | 用途 | NuttX での実装 |
|---|---|---|
| `wireguard_sys_now()` | タイマー用の単調増加ミリ秒カウンタ | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | 鍵生成用の暗号乱数 | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | リプレイ防止用の TAI64N タイムスタンプ | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | Cookie リプライの判定 | `return false` |

ここに OS 依存を隔離したことが、4 アーキテクチャをコード変更なしで移れた理由。
どのファイルがサードパーティ／改変あり／自作かは
[移植先の README](nuttx_port/apps/netutils/wireguard/README.md) にまとめてある。

---

## 残作業

| | 追跡 |
|---|---|
| PR を出す前に `dev@nuttx.apache.org` へ設計を共有する | [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3) |
| FLAT ビルド前提をどう扱うか決める | [#6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6) |
| `LICENSE` に wireguard-lwip の著作権表示を追記する | [#7](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/7) |
| ベースにする NuttX のバージョンを決める | [#8](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/8) |
| 長時間動作・異常系の検証（keepalive・再接続・MTU 境界） | [#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5) |

コーディングスタイル（`checkpatch.sh`・`nxstyle`）は全ファイルクリーン、流用ソースは
ツリーに取り込んで upstream と一致を確認済み、ディレクトリ構成も PR の形になっている。
方針は [docs/upstream/upstream-strategy.md](docs/upstream/upstream-strategy.md)、`dev@` への投稿ドラフトは
[docs/upstream/dev-list-proposal.md](docs/upstream/dev-list-proposal.md)。

その先: IPv6、ESP32-S3 の暗号アクセラレータ、Raspberry Pi Pico 2 W（RP2350 側に CYW43439
ドライバの移植が必要 — [#1](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/1),
[#2](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/2)）。

---

## ドキュメント

| | |
|---|---|
| [docs/design.html](docs/design.html) | 設計ドキュメント（図表ベース） |
| [docs/presentation/slides.html](docs/presentation/slides.html) | 発表スライド 27 枚（`N` キーで発表者ノート） |
| [docs/presentation/talk-script.md](docs/presentation/talk-script.md) | 発表台本（時間配分・削る順番つき） |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 現状・ビルド方法・テスト方法 |
| [docs/upstream/upstream-strategy.md](docs/upstream/upstream-strategy.md) | 提出計画 |
| [docs/development/hardware-verification.md](docs/development/hardware-verification.md) | 各ボードで何ができて何ができなかったか |
| [docs/development/phase4-log.md](docs/development/phase4-log.md) | 立ち上げの記録（失敗も含む） |

---

## 発表

[Community Over Code Glasgow 2026](https://communityovercode.org/)（10月11〜14日）併設の
**NuttX International Workshop** での発表を予定。CFP 提出済み。

---

## 参照実装

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip)** — 移植元のコード本体。
3 つの層で移植コストがまったく違う:

| ファイル | 役割 | 移植コスト |
|---|---|---|
| `wireguard.c` + `crypto/` | プロトコルと暗号 | なし — ポータブルな C。そのまま使用 |
| `wireguard-platform.h` | OS 依存（時刻・乱数・タイマー） | 小 — 4 関数のみ |
| `wireguardif.c` | lwIP netif グルー | 全面 — `nuttx-wireguardif.c` で置き換え |

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino)** —
同じコードを ESP32（FreeRTOS + ESP-IDF）に移植した先例。OS 固有のどこに注意が要るかの
参考として読んだ。

---

## 自己紹介

ソニーセミコンダクタソリューションズでエッジ AI エンジニアとして勤務。SPRESENSE および
ESP32 ベースのエッジ AI カメラシステムを中心に、アプリケーション側から NuttX を使っている。
NuttX ボードにリモートから安全にアクセスしてデバッグ・メンテナンスしたい、というのは
自分自身が欲しかったもの。

- NuttX: SPRESENSE・ESP32 での日常利用
- 組み込み C: `arm-none-eabi-gcc` によるクロスコンパイル、RTOS 上での POSIX API
- 資格: GCP・AWS・TensorFlow Developer・情報処理安全確保支援士

GitHub: [wwlapaki310](https://github.com/wwlapaki310)
