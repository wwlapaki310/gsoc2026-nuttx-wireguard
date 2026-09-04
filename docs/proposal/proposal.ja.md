# WireGuard Port to Apache NuttX
**(project: Apache Software Foundation)**

**Satoru Akita**
created: 3/31/2026

---

## 1. 応募者情報

| | |
|---|---|
| 👤 **氏名** | 秋田 賢 |
| 📧 **メール（main）** | wwlap24@gmail.com |
| 📧 **メール（sub）** | Satoru.Akita@sony.com |
| 💬 **Discord** | fox_aki310 |
| 🐙 **GitHub** | [@wwlapaki310](https://github.com/wwlapaki310) |
| 📁 **プロポーザルリポジトリ** | [wwlapaki310/gsoc2026-nuttx-wireguard](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard) |
| 🗨️ **NuttX ディスカッション** | [apache/nuttx#18548](https://github.com/apache/nuttx/issues/18548) |
| 🌏 **タイムゾーン** | JST（UTC+9）、日本 |

---

## 2. プロジェクト概要

Apache NuttX はリソース制約の厳しい環境向けに設計された POSIX 準拠のリアルタイム OS（RTOS）である。PX4 ベースの UAV から Sony SPRESENSE を活用したナノ衛星、ESP32 上で動作するエッジ AI カメラまで、重要な組み込みシステムを支えている。これらのデバイスがリモートや信頼されていないネットワークに展開されることが増える中、ファームウェア更新・診断・遠隔メンテナンスのための安全な暗号化トンネルは重要な要件となっている。しかし現在の NuttX ネットワークスタックには大きな欠落がある——軽量なネイティブ VPN 機能が存在しないことだ。

本プロジェクトはこの課題を解決するため、WireGuard を Apache NuttX に移植することを目的とする。WireGuard はモダンで高性能な VPN であり、Curve25519・ChaCha20-Poly1305・BLAKE2s という最新の暗号方式を使いながら、最小限のコードベースを維持している。従来のソリューションとは異なり、SSH と同程度のシンプルさで設定でき、マイコン上でも高効率に動作する。Linux カーネル 5.6（2020年）への統合以降、OpenWrt や ESP32 など組み込み環境への展開でもその実力が証明されている。

WireGuard を Apache NuttX に移植することで、最小限のリソースオーバーヘッドでセキュアかつプロダクション品質の通信を組み込みシステムに展開できるようになり、既存の RTOS 機能とモダンなセキュリティ標準の間のギャップを埋める。


Apache NuttX コミュニティへの貢献：

- **NuttX に欠けていた VPN 機能が追加される。** 本実装により、標準的な VPN 手段が NuttX に初めて提供される。

- **世界中の NuttX 開発者がすぐに使える状態で提供される。** 本プロジェクトのコードは Apache の公式リポジトリ（`apache/nuttx-apps`）への取り込みを目標とする。これにより、NuttX 開発者は自分でポーティング作業をすることなく、ビルド設定で WireGuard を有効化するだけで利用できるようになる。

- **lwIP ベースのネットワークライブラリを NuttX に持ち込む道筋を示す。** FreeRTOS や ESP-IDF など多くの組み込み環境では lwIP というネットワークライブラリが広く使われており、Mongoose Web Server（組み込み HTTP/WebSocket）・libcoap（IoT 向け CoAP）・Eclipse Paho Embedded MQTT など、lwIP 向けに書かれたネットワーク関連のソフトウェアが数多く存在する。NuttX は独自のネットワークスタックを持つため、これらをそのまま動かす方法がこれまでなかった。本プロジェクトでその橋渡しの手法を実証・文書化することで、WireGuard 以外の lwIP ベースのソフトウェアを NuttX に移植したい将来の開発者にとっての参考事例となる。

プロジェクトの成果は **Community Over Code Glasgow の NuttX International Workshop**（2026年10月11〜14日）で発表を目指す予定で、CFP はすでに提出済みである。

---

## 3. 提案詳細


| | |
|---|---|
| **組織** | [Apache Software Foundation](https://summerofcode.withgoogle.com/programs/2026/organizations/apache-software-foundation) |
| **難易度** | Major |
| **規模** | 約175時間（Medium） |
| **メンター** | Alan Carvalho de Assis (acassis@apache.org), dev@nuttx.apache.org |

### 3.1 参考事例

2つの既存プロジェクトを参照として使用する。それぞれ役割が異なる。

**[smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) — 移植元のコード本体**

NuttX に持ち込む実際のコード。WireGuard を lwIP の netif（network interface）として実装している。netif とは lwIP における仮想 NIC の抽象であり、`eth0` や `wlan0` と同列に扱われるネットワークインターフェースの単位である。WireGuard を netif として実装することで、上位のネットワークスタックからは通常の NIC と同様に見え、ルーティングやパケット転送が透過的に機能する。

OS 固有の処理はすべて4関数のプラットフォーム抽象層（`wireguard-platform.h`）に集約されており、WireGuard プロトコル本体（`wireguard.c`）と暗号実装（`crypto/`）は OS 依存がなくそのまま使用できる。移植作業の中心は2つである。1つは `wireguard-platform.h` の NuttX 向け実装（時刻・乱数・タイマー API の置き換え）。もう1つは `wireguardif.c` に含まれる lwIP 固有 API を NuttX の対応 API に置き換えること（`struct netif` → `struct net_driver_s`、`pbuf_alloc()` → `iob_alloc()`、`udp_new()/bind()` → BSD `socket()/bind()` など）。

**[ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino) — 移植の先例**

wireguard-lwip を ESP32（FreeRTOS + ESP-IDF）に移植したプロジェクト。NuttX も組み込み RTOS + lwIP という構成であるため、この ESP32 移植で行われた変更（FreeRTOS プリミティブの置き換え・ログ出力の変更・プラットフォーム固有ヘッダの削除）は、wireguard-lwip を新しいターゲットに移植する際に何を変える必要があるかを示す具体的な参照として使える。


---

### 3.2 タスク一覧

**Phase 0〜1（応募前に完了済み）**
- Docker 開発環境の構築（`sim` / `qemu-armv7a` の2ターゲット）
- `apps/netutils/wireguard/` のビルドシステム整備（`CMakeLists.txt`・`Make.defs`・`Kconfig`）
- `wireguard.c` を無修正でコンパイルするための最小限の lwIP 互換シムヘッダー
- `wireguardif.c` 内の全 lwIP 呼び出しと NuttX 対応 API のマッピング
- ✅ `CONFIG_NET_WIREGUARD=y` でビルドが通り、SIM 上で `nsh>` に到達

**Phase 2 — NuttX 統合層の実装（6月16日〜7月4日）**
- `nuttx-platform.c`：時刻・乱数・タイマーの OS 固有4関数を NuttX POSIX API で実装
- `nuttx-wireguardif.c`：lwIP 固有 API（`struct netif`・`pbuf`・`udp_*`）を NuttX の `net_driver_s`・`iob`・BSD socket に置き換え
- ✅ SIM 上で `nsh> ifconfig` に `wg0` が表示される

**Phase 3 — QEMU でのハンドシェイクと疎通確認（7月14日〜8月1日）★ 中間評価**
- SIM 構成を `qemu-armv7a` に移植（NuttX 自身のスケジューラ上での動作確認）
- Linux ピアとの WireGuard ハンドシェイク確立
- ✅ `nsh> ping 10.0.0.1` が通り、Linux 側 `wg show` でハンドシェイクが確認できる

**Phase 4 — NSH コマンドと ESP32-S3 実機検証（8月4日〜9月5日）**
- `wg show` / `wg setconf` を NSH ビルトインコマンドとして実装
- ESP32-S3 実機で Wi-Fi 経由の WireGuard トンネルを確立
- ✅ 実機での疎通確認、Flash・RAM 使用量の実測値取得

**Phase 5 — アップストリーム PR（9月8日〜27日）**
- Apache CLA に署名
- `apache/nuttx-apps` の `apps/netutils/wireguard/` に PR を提出
- ✅ PR オープン

---

### 3.3 タイムライン

フルタイム社会人であり、週12〜15時間、バッファ2週間を想定した取り組みをする予定。

| 期間 | 日程 | 内容 |
|------|------|------|
| コミュニティボンディング | 5月1日〜24日 | Phase 0：API マッピング確定、メンターとの方針合意 |
| Phase 1 | ✅ 応募前に完了済み | ビルドシステム統合 |
| 1〜5週目 | 5月25日〜6月27日 | Phase 2：NuttX 統合層の実装（SIM） |
| バッファ | 6月30日〜7月4日 | 調整・巻き返し |
| ★ 中間評価 | 7月6日〜10日 | Phase 2 成果物提出（`wg0` が `ifconfig` に表示） |
| 7〜10週目 | 7月14日〜8月8日 | Phase 3：QEMU ハンドシェイクと疎通確認 |
| （お盆休み） | 8月8日〜15日 | 休暇 |
| 11〜14週目 | 8月18日〜9月12日 | Phase 4：NSH コマンド + ESP32-S3 実機 |
| 15〜18週目 | 9月15日〜30日 | Phase 5：アップストリーム PR |
| カンファレンス準備 | 10月1日〜10日 | ASF Conference 準備 |
| カンファレンス | 10月11日〜14日 | ASF Conference @ Glasgow（CFP 提出済み） |

---

## 4. コミュニケーション

Timezone: UTC+9 (Japan Standard Time) / 10:00 ~ 18:00

### 4.1 コミュニケーション方針

リモートおよびハイブリッドワークの経験から、タイムゾーンを跨いだ非同期コミュニケーションには慣れている。業務では Microsoft Teams をメインに使用し、個人活動では Discord・Slack・Google Meet・Zoom・X (Twitter) など多様なツールを状況に応じて使い分けているため、メンターやコミュニティが推奨する連絡手段があれば柔軟に対応可能である。

本プロジェクトでは、以下の運用を基本方針とする。

- **GitHub Issues / PR コメント：** 技術的な議論やコードレビューの主戦場とする。議論の過程をオープンに保ち、コミュニティ全体が参照できる形にする。
- **Discord：** クイックな質疑応答や同期的なミーティングに使用する。メンターとのやり取りを円滑にするため、専用の Discord チャンネルを作成することを提案したい。
- **週次進捗報告（Weekly Report）：** 専用の Discord チャンネルまたは GitHub 上で毎週の進捗を報告する。進捗だけでなく、技術的なブロッキングポイントを早期に共有することで、プロジェクトの停滞を防ぐ。

| 手段 | 用途 |
|------|------|
| 📧 メール（wwlap24@gmail.com） | 緊急時の連絡および公式な通知用。24時間以内に返信する |
| 💬 Discord（fox_aki310） | 日常的なコミュニケーション、週次の同期確認、進捗報告 |
| 🗨️ GitHub Issues / PRs | 技術議論、コードレビュー、タスク管理 |
| 📋 NuttX メーリングリスト | コミュニティ全体への重要なアップデート報告や議論 |



### 4.2 英語力

- 英語は第二言語だが、テキストベースのコミュニケーション（チャット・メール・コードレビュー）は問題なく行える。スピーキングは日々向上中。
- SXSW ハッカソンおよび修士論文発表で英語プレゼンの経験がある
- Sony Europa（スウェーデン・Lund）のチームと協働して AITRIOS サンプルアプリケーションを開発し、OSS として GitHub に公開した経験はある。

---

## 5. 自己紹介

| | |
|---|---|
| 📄 **CV** | [Google Drive](https://drive.google.com/file/d/1WaaCUJOFb_DxdXQ1hG7ZQF_cu7Jbm_pr/view) |
| 💼 **LinkedIn** | [satoru-akita-6070a4145](https://www.linkedin.com/in/satoru-akita-6070a4145/) |
| 🏢 **勤務先** | [ソニーセミコンダクタソリューションズ株式会社](https://www.sony-semicon.com/ja/index.html) |
| 🎓 **学歴** | M.S. in Robotics, [東北大学](http://www.mems.mech.tohoku.ac.jp/index_e.html) |
| 📝 **ブログ** | [wwlapaki310.github.io](https://wwlapaki310.github.io/) |

### 5.1 応募動機

私はソニーセミコンダクタソリューションズにて、NuttX が実際に使われている2つのプロダクト領域に携わっている。

**AITRIOS（エッジ AI カメラプラットフォーム）**

Sony IMX500 インテリジェントビジョンセンサーと ESP32 を組み合わせたエッジ AI カメラを核とするプラットフォームで、ESP32 上では NuttX が動作している。このカメラは物流倉庫・小売店舗・交通モニタリングなどの現場に設置され、フィールドで長期間稼働する。
参考：https://www.aitrios.sony-semicon.com/edge-ai-devices

**SPRESENSE（低消費電力マイコン）**

Sony が開発・展開する小型・低消費電力のマイコンボードで、人工衛星や海洋モニタリングなどのミッションクリティカルな用途での採用実績がある。代表例として、2023年に打ち上げられた月面変形探査ロボット SORA-Q は SPRESENSE を搭載しており、また JAXA（Japan Aerospace Exploration Agency）との連携による小型人工衛星プロジェクトも進行中である。
参考：https://www.hackster.io/news/sora-q-the-sony-spresense-powered-transforming-robot-heads-moonward-if-spacex-can-fix-falcon-9-c81e490e78b1

---

これらのプロジェクトに実際に携わる中で、遠隔地に設置された NuttX デバイスに安全にアクセスしたいという課題に何度も直面してきた。物流倉庫に設置されたカメラのファームウェア更新、衛星のリモートデバッグ——いずれも、物理的なアクセスを前提にしない安全な通信手段を必要とする。WireGuard は Linux 上でこの問題を解決している。それを NuttX 上でも動かしたいというのが、本プロジェクトへの直接の動機である。

また、NuttX を毎日使うユーザーとして、オープンソースコミュニティに具体的な機能貢献をしたいという気持ちもある。
AITRIOSサンプルアプリをOSS公開したことはあるが、既存のOSSにコミットするのは初めての経験である。
Apache の公式リポジトリへの取り込みを通じて、OSS貢献の経験を積み、世界のSWエコシステムを支えるエンジニアになりたい。


### 5.2 稼働時間・可用性

- **稼働時間：** 週約12~15時間（平日夜 + 週末）
- **不在期間：** 8月8日〜15日（お盆休み）
- **最終提出：** 9月末での提出を想定。余裕があれば、8月25日（GSoC 公式締め切り）に間に合うよう進める。
- **GSoC 期間後：** ASF Conference @ Glasgowにて発表する。

### 5.3 趣味・経験

ソフトウェアカンファレンスへの参加・コミュニティ運営・ハッカソンを日常的に行っている。Open Source Summit Japan では運営スタッフとして携わり、Unitree G1 を使ったロボットハッカソンにも参加した。

![Open Source Summit Japan 運営](../../assets/oss-summit-japan.jpg)
![Unitree G1 ロボットハッカソン](../../assets/unitree-g1-hackathon.jpg)

もともと宇宙が好きで、学生時代はロケットや自律ロボットの製作に取り組んでいた。それが SPRESENSE や NuttX を使ったエッジ AI・宇宙機開発への関心につながっている。そのほか、マラソン・ゴルフ・旅行・歴史が好き。

### 5.4 Community Over Code Glasgow への応募について

ソニーグループ内のチャットにて Jerpelea Alin さんと会話する機会があり、背中を押していただいて本応募を進めることができた。

本プロジェクトの成果発表として、Community Over Code Glasgow（2026年10月11〜14日）の NuttX International Workshop に CFP を提出済みである。

- CFP：[docs/proposal for ASF2026.md](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/blob/main/docs/proposal%20for%20ASF2026.md)

採択されるかはまだわからないが、ぜひ Glasgow でお会いできることを楽しみにしている。



