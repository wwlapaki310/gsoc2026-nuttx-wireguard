# GSoC 2026 — WireGuard の Apache NuttX への移植

| | |
|---|---|
| **組織** | [Apache Software Foundation](https://summerofcode.withgoogle.com/programs/2026/organizations/apache-software-foundation) |
| **難易度** | Major |
| **規模** | 約175時間（Medium） |
| **メンター** | Alan Carvalho de Assis (acassis@apache.org), dev@nuttx.apache.org |
| **参照実装** | [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip), [ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) |

---

## プロジェクト概要

WireGuard は Linux 向けに開発された軽量な VPN プロトコルで、組み込み・IoT 分野でも採用が広がっている。UDP 上で暗号化トンネルを確立し、使用する暗号アルゴリズムは Curve25519・ChaCha20-Poly1305・BLAKE2s。実装がコンパクトでマイコン上での動作にも適している。

Apache NuttX は POSIX 準拠の RTOS で、lwIP TCP/IP スタックによるネットワーク機能を持つ。しかし現時点では VPN 機能が存在しない。

本プロジェクトでは WireGuard を **lwIP の仮想ネットワークインターフェース（netif）** として実装し、NuttX に移植する。参照実装として [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) を使用する。これはすでに lwIP ベースの WireGuard 実装であり、NuttX への移植起点として適している。

### なぜこのプロジェクトが必要か

NuttX デバイスへの遠隔・安全なアクセスは多くの分野で未解決の課題となっている。

- **エッジ AI・産業 IoT** — フィールドに展開されたデバイスに対して、物理的なアクセスなしにファームウェア更新や遠隔診断を行う必要がある
- **衛星・宇宙機** — NuttX は小型衛星プロジェクトで採用されている。打ち上げ後はネットワーク経由が唯一のメンテナンス手段になる
- **無人インフラ** — 海洋ブイ・山岳観測所・パイプラインなど遠隔地のセンサーには安全な双方向通信が必要
- **デバイス間直接通信** — クラウド中継なしに NuttX デバイス同士が暗号化トンネルで直接通信できる

WireGuard のコンパクトな実装とシンプルな鍵モデルは、こうした制約のある環境に適している。

### アーキテクチャ

```
+----------------------------------+
|          NuttX RTOS              |
|                                  |
|  Application / NSH               |
|           |                      |
|      lwIP TCP/IP Stack           |
|       |            |             |
|   eth0 / wlan0    wg0            |  ← WireGuard netif（本プロジェクト）
|   (物理 NIC)    (VPN トンネル)    |
|                   |              |
|        UDP ソケット（ポート 51820）|
+----------------------------------+
            |
     インターネット / LTE / 衛星回線
            |
     WireGuard ピア（Linux サーバー）
```

---

## 参照実装

2つの既存プロジェクトを参照として使用する。それぞれ役割が異なる。

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) — 移植元のコード本体**

NuttX に持ち込む実際のコード。WireGuard を lwIP の netif として実装しており、OS 固有の処理はすべて4関数のプラットフォーム抽象層（`wireguard-platform.h`）に集約されている。WireGuard プロトコル本体（`wireguard.c`）と暗号実装（`crypto/`）は OS 依存がなくそのまま使用できる。移植作業の中心は `wireguard-platform.h` を NuttX 向けに実装することと、`wireguardif.c` からプラットフォーム固有のコードを取り除くことである。

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) — 移植の先例**

wireguard-lwip を ESP32（FreeRTOS + ESP-IDF）に移植したプロジェクト。NuttX も組み込み RTOS + lwIP という構成であるため、この ESP32 移植で行われた変更（FreeRTOS プリミティブの置き換え・ログ出力の変更・プラットフォーム固有ヘッダの削除）は、wireguard-lwip を新しいターゲットに移植する際に何を変える必要があるかを示す具体的な参照として使える。

---

## 開発タイムライン

> **注:** このタイムラインは検討中であり、メンターとの議論を経て変更される可能性がある。

筆者は日本在住（JST / UTC+9）。作業時間に余裕を持たせるため、タイムラインには作業できない期間への柔軟性を持たせてある。

### Phase 0 — 準備（GSoC 開始前 / コミュニティボンディング期間）

**目標:** NuttX 固有のコードを書き始める前に、必要な理解と環境を整える。

Phase 4 まではすべて QEMU 上で進める。実機テストは Phase 5 まで持ち越す。イテレーションを速く回すため、実機に依存しない環境で開発する。

**応募前に完了済み:**
- Docker + QEMU 開発環境の構築（NuttX `qemu-armv7a:nsh`・ネットワーク有効化済み）（本リポジトリの `Dockerfile` 参照）
- wireguard-lwip と WireGuard-ESP32-Arduino のソースコードを読み、移植スコープを把握

**コミュニティボンディング期間中に完了予定:**
- wireguard-lwip 内で NuttX 向けに置き換えが必要な OS 固有 API をすべて洗い出す（スレッド・mutex・時刻・乱数）
- ESP32 移植を diff として読む: wireguard-lwip から FreeRTOS + ESP-IDF で動かすために何を変えたかを把握し、各変更を NuttX の対応 API にマッピングする
- ビルド＆テストループを整備: `apps/netutils/wireguard/` 内の wireguard ソースを NuttX イメージに組み込み、QEMU 上で変更を繰り返しテストできる仕組みを作る

**成果物:** 置き換えが必要な API の一覧ドキュメントと、QEMU 上で動作するビルドループ。

---

### Phase 1 — ビルドシステム統合（第1〜2週）

**目標:** wireguard-lwip のソースを NuttX のクロスコンパイルツールチェーンでコンパイルできる状態にする。

wireguard-lwip は `.c`/`.h` ソースファイルのみを提供しており、独自のビルドシステムを持たない。各ターゲットプラットフォームのビルドシステムに組み込むことを前提とした設計になっている。NuttX は CMake + Kconfig + make のハイブリッドビルドシステムを使用しており、`apps/netutils/wireguard/` 以下にソースを置いても `CMakeLists.txt`（または `Make.defs`）と `Kconfig` がなければビルドシステムがそのディレクトリを完全に無視する。

また、NuttX の ARM ターゲット向けビルドでは `arm-none-eabi-gcc` と newlib を使用する。型定義やヘッダが一般的なデスクトップ Linux ビルド環境と異なる場合があり、解消すべきコンパイルエラーが発生する可能性がある。

Phase 1 の完了条件は「コンパイルエラーがゼロになること」であり、リンクや実行は次フェーズ以降で扱う。

- wireguard-lwip のソースを `apps/netutils/wireguard/` 以下に配置
- NuttX の作法に従って `CMakeLists.txt` と `Make.defs` を記述
- `arm-none-eabi-gcc` / newlib 環境でのコンパイルエラーを解消（型定義の差異・ヘッダ不足・属性など）
- `Kconfig` エントリを追加: `CONFIG_NET_WIREGUARD`

**成果物:** `sim:nsh` ビルドで wireguard 関連のビルドエラーがゼロになること。

---

### Phase 2 — NuttX プラットフォーム層の実装（第3〜4週）

**目標:** `wireguard-platform.h` を実装する。wireguard-lwip が各移植先に要求するプラットフォーム抽象層。

| 関数 | 用途 | NuttX での実装 |
|------|------|---------------|
| `wireguard_sys_now()` | タイマー用の単調増加ミリ秒カウンタ | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | 鍵生成用の暗号乱数 | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | リプレイ攻撃防止用の TAI64N タイムスタンプ | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | Cookie リプライの判定 | `return false`（組み込みでは十分） |

- `nuttx-platform.c` を新規作成して4関数を実装
- `wireguardif.c` 内の ESP 固有ログ（`ESP_LOGI` 等）を `syslog()` に置き換え
- ESP 固有ヘッダ（`esp_netif.h`・`tcpip_adapter.h`）を削除

**成果物:** QEMU 上で `wireguardif_init()` がクラッシュせずに動作すること。

---

### Phase 3 — lwIP netif への登録（第5〜6週）

**目標:** WireGuard を NuttX の lwIP スタックに仮想 NIC として登録し、`ifconfig` で `wg0` が表示されるようにする。

- NuttX 起動時に `wireguardif_init()` を呼び出す
- WireGuard パラメータ（秘密鍵・待受ポート）を Kconfig で設定
- `netif_add()` が成功することを確認

**成果物:** `nsh> ifconfig` に `wg0` が表示されること。

---

### Phase 4 — QEMU 上でのハンドシェイクとトンネル疎通（第7〜9週）★ Midterm

**目標:** Linux ピアと WireGuard ハンドシェイクを完了し、暗号化トンネルを通じてトラフィックを通す（QEMU 上）。

- NuttX 側・Linux 側それぞれで鍵ペアを生成
- ピアの公開鍵とエンドポイントを Kconfig で NuttX に設定
- UDP ポート 51820 を使った Noise プロトコルのハンドシェイクを確認
- エンドツーエンドの疎通確認: `nsh> ping 10.0.0.1`

**成果物（Midterm）:**
```
# Linux 側:
$ sudo wg show
peer: <NuttX の公開鍵>
  latest handshake: 3 seconds ago

# NuttX (QEMU):
nsh> ping 10.0.0.1
64 bytes from 10.0.0.1: icmp_seq=0 time=4 ms
```

---

### Phase 5 — NSH コマンド・Kconfig 統合・実機テスト（第10〜11週）

**目標:** `wg` コマンドを NSH に追加し、ESP32-S3 実機での動作を確認する。

**NSH コマンド:**
- `wg show` と `wg setconf` を NSH ビルトインコマンドとして実装
- Kconfig の依存関係を整備: `NET_WIREGUARD` は `NET`・`NET_UDP`・`MBEDTLS` に依存

```
nsh> wg show
interface: wg0
  public key: <base64>
  listening port: 51820
peer: <base64>
  endpoint: 192.168.x.x:51820
  latest handshake: 5 seconds ago
  transfer: 1.23 KiB received, 0.45 KiB sent
```

**実機テスト（ESP32-S3）:**

QEMU は virtio-net ドライバ経由で lwIP に接続するが、ESP32-S3 は Wi-Fi ドライバ経由で lwIP に接続する。ネットワークスタックのパスが異なるため、実機での検証が必要。

- ESP32-S3 に NuttX + WireGuard イメージを書き込む
- Wi-Fi に接続し、`wlan0` と並んで `wg0` が起動することを確認
- Wi-Fi 経由で Linux ピアとの WireGuard トンネルを確立
- トンネル越しに `nsh> ping` が通ることを確認
- Flash・RAM の実測値を確認

**成果物:** ESP32-S3 の Wi-Fi 経由で WireGuard トンネルが動作すること。

---

### Phase 6 — upstream PR（第12週）

**目標:** `apache/nuttx-apps` にプルリクエストを提出する。

- Apache CLA に署名
- NuttX コーディングスタイルに準拠した PR を `apps/netutils/wireguard/` に提出

**成果物:** `apache/nuttx-apps` に PR がオープンされること。

---

## 自己紹介

ソニーセミコンダクタソリューションズでエッジ AI エンジニアとして勤務しており、SPRESENSE および ESP32 ベースのエッジ AI カメラシステムを中心にアプリケーション側から NuttX を使用している。NuttX ボードにリモートから安全にアクセスしてデバッグ・メンテナンスを行えるようにしたいと考えていたため、本プロジェクトは日常業務と直接つながっている。

**関連経験:**

- NuttX: SPRESENSE・ESP32 での日常利用
- 組み込み C: `arm-none-eabi-gcc` によるクロスコンパイル・RTOS 上での POSIX API
- Docker + QEMU: 本プロジェクト用に `qemu-armv7a:nsh` の開発環境を構築済み（本リポジトリ参照）
- 資格: GCP・AWS・TensorFlow Developer・情報処理安全確保支援士

GitHub: [https://github.com/wwlapaki310](https://github.com/wwlapaki310)
