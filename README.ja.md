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

## 開発タイムライン

### Phase 1 — ビルドシステム統合（第1〜2週）

**目標:** wireguard-lwip のソースを NuttX のクロスコンパイルツールチェーンでコンパイルできる状態にする。

- wireguard-lwip のソースを `apps/netutils/wireguard/` 以下に配置
- NuttX の作法に従って `CMakeLists.txt` と `Make.defs` を記述
- `arm-none-eabi-gcc` のコンパイルエラーを解消（型定義の差異・属性など）
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

### Phase 4 — ハンドシェイクとトンネル疎通（第7〜9週）★ Midterm

**目標:** Linux ピアと WireGuard ハンドシェイクを完了し、暗号化トンネルを通じてトラフィックを通す。

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

### Phase 5 — NSH コマンドと Kconfig 統合（第10〜11週）

**目標:** 実行時のステータス確認と設定のために `wg` コマンドを NSH に追加する。

- `wg show` と `wg setconf` を NSH ビルトインコマンドとして実装
- Kconfig の依存関係を整備: `NET_WIREGUARD` は `NET`・`NET_UDP`・`MBEDTLS` に依存

**成果物:**
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

---

### Phase 6 — 実機テストと upstream PR（第12週）

**目標:** 実機で動作確認し、`apache/nuttx-apps` にプルリクエストを提出する。

- ESP32-S3 で動作確認（Wi-Fi netif と WireGuard netif の共存）
- Flash・RAM の実測値を確認
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
