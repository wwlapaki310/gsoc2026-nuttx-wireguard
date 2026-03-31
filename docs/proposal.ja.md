# WireGuard Port to Apache NuttX
**(project: Apache Software Foundation)**

**Satoru Akita**
created: 3/30/2026

---

## 1. 応募者情報

| | |
|---|---|
| 👤 **氏名** | 秋田 悟 |
| 📧 **メール（main）** | wwlap24@gmail.com |
| 📧 **メール（sub）** | Satoru.Akita@sony.com |
| 💬 **Discord** | fox_aki310 |
| 🐙 **GitHub** | [@wwlapaki310](https://github.com/wwlapaki310) |
| 📁 **プロポーザルリポジトリ** | [wwlapaki310/gsoc2026-nuttx-wireguard](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard) |
| 🗨️ **NuttX ディスカッション** | [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548) |
| 🌏 **タイムゾーン** | JST（UTC+9）、日本 |

---

## 2. プロジェクト概要

NuttX は人工衛星、エッジ AI カメラ、ロボティクス、リモートセンシングシステムなど、安全なリモートアクセスが不可欠でありながら現状では実現できていない分野で広く使われている。本プロジェクトでは WireGuard を NuttX に移植し、ネイティブなネットワークデバイス（`wg0`）として実装する。これにより、NuttX デバイスがピア側の標準的な WireGuard ツールを使って暗号化 VPN トンネルを確立できるようになる。

NuttX は物理的なアクセスが困難または不可能な環境で動作している。ファームウェアアップデート、リモートデバッグ、安全なデバイス間通信を実現するには、マイコン上で動作できる小さな VPN と、静的な鍵ペアで設定できるシンプルな仕組みが必要となる。WireGuard はコードベースが小さく、暗号化方式は最新であり、鍵モデルはシンプルで、Linux デバイスでは大規模に運用実績がある。NuttX への移植により、同様の機能が組み込み RTOS の世界にもたらされる。

Apache NuttX コミュニティへの具体的な貢献：

- **エコシステムのギャップを埋める。** NuttX は現時点で VPN 機能を持たない。多くのユーザーが手動で回避策を講じてきたこの問題が解決される。
- **実装はアップストリームに取り込まれる。** 目標は `apache/nuttx-apps` への PR であり、任意の NuttX ユーザーが `CONFIG_NET_WIREGUARD=y` で有効化できる形を目指す。
- **アプローチ自体がリファレンスになる。** `nuttx-wireguardif.c` の統合層は、lwIP ベースのネットワークコンポーネントを NuttX のネイティブ netdev API に接続する方法を実証・文書化したものであり、将来の移植作業の参考にもなる。

プロジェクトの成果は **Community Over Code Glasgow の NuttX International Workshop**（2026年10月11〜14日）で発表する予定であり、CFP はすでに提出済みである。

---

## 3. 提案内容

### 3.1 先行事例

**smartalock/wireguard-lwip** (https://github.com/smartalock/wireguard-lwip)

本プロジェクトの主要な参照実装であり、ポータブルなコードの提供元でもある。WireGuard を lwIP の netif として実装しており、OS 固有の挙動を `wireguard-platform.h` の4つの関数の背後に隔離している。プロトコルコア（`wireguard.c`）と暗号プリミティブ（`crypto/`）は OS・アーキテクチャ非依存のポータブル C で書かれており、変更不要で利用できる。

wireguard-lwip のコードは3層に分かれており、移植コストはそれぞれ異なる：

| ファイル | 役割 | 移植コスト |
|---------|------|-----------|
| `wireguard.c` + `crypto/` | WireGuard プロトコル本体・暗号実装 | なし（OS 非依存、そのまま使用） |
| `wireguard-platform.h` | OS 固有の4関数（時刻・乱数・タイマー・負荷判定） | 低（NuttX の POSIX API に置き換え） |
| `wireguardif.c` | ネットワーク統合層（lwIP API 呼び出し） | 中（ロジックを再利用しつつ、lwIP API を NuttX API に置き換え） |

**ciniml/WireGuard-ESP32-Arduino** (https://github.com/ciniml/WireGuard-ESP32-Arduino)

FreeRTOS + ESP-IDF 上の ESP32 への wireguard-lwip の移植実装。ESP-IDF が lwIP を使用しているため、この移植では `wireguardif.c` をより直接的に利用できる。OS 固有のプラットフォーム層（`wireguard-platform.h`）——特にタイマー API、乱数生成、組み込み RTOS での mutex パターン——の参考として活用する。

**本プロジェクトとの違い：**

Phase 1 の調査で判明したとおり、NuttX は lwIP を使用していない。NuttX は uIP 由来ではあるが、現在はほぼ独自設計の TCP/IP スタックを持っており、`lwip/netif.h` のような lwIP のパブリックヘッダーはインクルードパスに存在しない。このため、`wireguardif.c` は NuttX 上でそのままコンパイルできない。本プロジェクトの貢献は、lwIP ベースのネットワークコンポーネントを NuttX のネイティブ netdev・socket API に接続する方法を実証し文書化する点にあり、WireGuard にとどまらずコミュニティ全体で再利用可能なパターンとなる。

---

### 3.2 取り組み内容と成果物

**応募前に完了済みの作業（Phase 0 〜 Phase 1）：**

- Docker ベースの開発環境（2つのビルドターゲット：TUN/TAP ネットワーク付きの `sim` と `qemu-armv7a`）
- `CONFIG_NET_WIREGUARD=y` でエラーなくビルドが通り、SIM 上で NuttX が `nsh>` まで起動し `eth0` が確認できる状態
- `apps/netutils/wireguard/` のビルドシステムファイル（`CMakeLists.txt`、`Make.defs`、`Makefile`、`Kconfig`）
- `wireguard.c` を無修正でコンパイルするための最小限の lwIP 互換シムヘッダー
- API マッピング：`wireguardif.c` 内の全 lwIP 呼び出しと NuttX 対応 API の対応表（`docs/phase1-log.md` に文書化済み）

**Phase 2 — SIM 上での NuttX 統合層の実装（6月16日〜7月4日）【必須】**

残る2ファイルを実装する。

*nuttx-platform.c* — OS 固有の挙動を置き換える4つの関数：

| 関数 | NuttX での実装 |
|------|---------------|
| `wireguard_sys_now()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | `return false` |

*nuttx-wireguardif.c* — ネットワーク統合層：

| lwIP（wireguardif.c） | NuttX（nuttx-wireguardif.c） |
|-----------------------|------------------------------|
| `struct netif` | `struct net_driver_s` |
| `netif_add()` | `netdev_register()` |
| `ip_input(pbuf, netif)` | `devif_input(dev)` |
| `udp_new()` / `udp_bind()` / `udp_recv()` | BSD `socket()` / `bind()` / `recvfrom()` |
| `pbuf_alloc()` / `pbuf_free()` | `iob_alloc()` / `iob_free()` |
| `sys_timeout()` | `wd_start()` |

✅ 成果物：SIM 上で `nsh> ifconfig` に `eth0` と並んで `wg0` が表示される

**Phase 3 — QEMU でのハンドシェイクとトンネル疎通（7月14日〜8月1日）★ 中間評価【必須】**

SIM 構成を `qemu-armv7a` に移植し、Linux ピアとの WireGuard ハンドシェイクとエンドツーエンドの疎通を確認する。QEMU は NuttX 自身のスケジューラが動作するため、タイマーベースの動作（キープアライブ、ハンドシェイク期限切れ）の検証に必要。

✅ 成果物：
```
# Linux 側:
$ sudo wg show
peer: <NuttX の公開鍵>
  latest handshake: 3 seconds ago

# NuttX（QEMU）側:
nsh> ping 10.0.0.1
64 bytes from 10.0.0.1: icmp_seq=0 time=4 ms
```

**Phase 4 — NSH コマンドと実機検証（8月4日〜9月5日）【必須】**

NSH 組み込みコマンド `wg show` / `wg setconf` を実装し、ESP32-S3 実機での Wi-Fi 経由トンネル確立を検証する。

✅ 成果物：ESP32-S3 上で Wi-Fi 経由の WireGuard トンネルが動作すること、Flash・RAM 使用量の実測値

**Phase 5 — アップストリーム PR とドキュメント整備（9月8日〜27日）【必須】**

Apache CLA に署名し、`apache/nuttx-apps` の `apps/netutils/wireguard/` に PR を提出。レビューフィードバックに対応しドキュメントを整備する。

✅ 成果物：`apache/nuttx-apps` への PR オープン

---

### 3.3 タイムライン

| 期間 | 日程 | 内容 |
|------|------|------|
| コミュニティボンディング | 5月8日〜6月1日 | Phase 0：API マッピング確定、メンターとの方針合意 |
| 1〜2週目 | 6月2日〜13日 | Phase 1 ✅ 応募前に完了済み |
| 3〜5週目 | 6月16日〜7月4日 | Phase 2：NuttX 統合層の実装（SIM） |
| バッファ | 7月7日〜11日 | 調査・巻き返し |
| 6〜8週目 | 7月14日〜8月1日 | Phase 3：QEMU ハンドシェイク ★ 中間評価 |
| 9〜11週目 | 8月4〜8日、8月18日〜9月5日 | Phase 4：NSH コマンド + ESP32-S3 実機 |
| （お盆休み） | 8月8日〜15日 | 不在 |
| GSoC 最終提出 | 8月25日 | — |
| 12〜14週目 | 9月8日〜27日 | Phase 5：アップストリーム PR |
| カンファレンス | 10月11日〜14日 | ASF Conference @ Glasgow（CFP 提出済み） |

---

## 4. コミュニケーション

### 4.1 連絡手段・方針

| 手段 | 用途 |
|------|------|
| 📧 メール（wwlap24@gmail.com） | メインの連絡先。24時間以内に返信する。 |
| 💬 Discord（fox_aki310） | 迅速なやり取りや進捗共有 |
| 🗨️ GitHub Issue / PR コメント | 技術的な議論・コードレビュー |
| 📋 NuttX メーリングリスト | コミュニティへの報告・議論 |

週次で進捗報告をメンターに送る。問題が発生した場合は早めに共有し、ブロッカーを放置しない。

### 4.2 英語力

業務では英語の技術文書を日常的に読み書きしており、メール・GitHub でのやり取りに支障はない。口頭コミュニケーション（ビデオ通話など）も対応可能。今回のプロポーザルおよびリポジトリの英語版ドキュメントは自身で作成した。

---

## 5. 自己紹介

### 5.1 技術経験

ソニーセミコンダクタソリューションズにてエッジ AI エンジニアとして勤務している。日々の業務では Sony IMX500 インテリジェントビジョンセンサーと、その上で動作する SPRESENSE および ESP32 ベースのカメラシステムを扱っており、これらのプラットフォーム上で Apache NuttX をアプリケーション側から日常的に使用している。

**本プロジェクトに関連する技術スキル：**

- **組み込み C：** `arm-none-eabi-gcc` によるクロスコンパイル、RTOS 上での POSIX API、レジスタレベルのハードウェア操作を日常的に行っている
- **NuttX：** SPRESENSE および ESP32 上でのアプリケーション開発の経験あり。ビルドシステム（Kconfig、CMake、make）、NSH、netdev・socket API に精通している
- **ネットワーク：** TCP/IP スタックの基礎、BSD socket API、UDP、VPN の概念
- **Docker + QEMU：** 本プロジェクトのために `sim` および `qemu-armv7a` の開発環境をゼロから構築した
- **C 言語の実力：** 組み込み文脈での低レベル C コードの読み書きに慣れており、プロポーザル準備の一環として wireguard-lwip および WireGuard-ESP32-Arduino のコードベースを通読した

**資格：** GCP Professional、AWS Certified、TensorFlow Developer Certificate、情報処理安全確保支援士（IPA）

**主な実績：**

- SPAJAM 2024 優秀賞（全国大会決勝、NHK 放映）
- Harvard BIOMD 2015 グランプリ
- NASA Space Apps Challenge 2020 東京大会 優勝
- ICAN 2014 世界第3位

### 5.2 稼働時間・可用性

- **稼働時間：** 週約15時間（平日夜 + 週末、JST）
- **不在期間：** 8月8日〜15日（お盆休み）
- **最終提出：** 8月25日（GSoC 公式締め切り）に間に合うよう進める
- **GSoC 期間後：** 9月末までアップストリーム PR のフォローを継続する

### 5.3 応募動機

ファームウェアアップデート、設定変更、診断データの取得など、フィールドに展開された NuttX デバイスに安全にアクセスする必要が生じたとき、すっきりした解決策がないという問題に何度も直面してきた。WireGuard は Linux 上でこの問題をまさに解決している。それを NuttX 上でも動かしたい——これが本プロジェクトへの動機である。

また、ソニーの仕事で NuttX を毎日使っているユーザーとして、コミュニティに具体的な機能貢献をしたいという気持ちもある。単に動くものを作るだけでなく、`apache/nuttx-apps` へのアップストリーム PR を通じて、すべての NuttX ユーザーが使える形にすることを最終目標としている。
