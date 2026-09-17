# Sechack365 Returns talk: WireGuard for Apache NuttX

HTML deck: [returns-slides.html](../returns-slides.html) — 8 枚

Audience: Sechack365 Returns alumni event。**スライドの文字は英語、話すのは日本語。**
このファイルが Returns デッキの単一の情報源で、構成・各スライドに載せるもの・話すこと・出典を
すべてここに書く。デッキ内のスピーカーノート（`N` キー）は英語のまま残してあるので、
本番で読むのはこのファイルの「話すこと」のほうを使う。

本編 27 枚のデッキは [slides.html](../slides.html)、その台本は [talkscript/slides.md](slides.md)。
短い版のデッキは [short-slides.html](../short-slides.html)（9 枚）、その台本は [short-slides.md](short-slides.md)。

## Core Message

> A WireGuard VPN implementation for Apache NuttX, exposed as a `wg0` network device.
> Verified on real hardware against real WireGuard peers.

（[README.md](../../../README.md) 冒頭 / [README.ja.md](../../../README.ja.md)「Apache NuttX 上で動く
WireGuard VPN の実装。`wg0` というネットワークデバイスとして見える。実機で、本物の WireGuard
ピアを相手に検証済み。」）

---

## 話の順番

1. **NuttX に WireGuard を実装した** — 何を作ったか。WireGuard 自体の簡単な説明と図
2. **ASF と Community Over Code とは** — 組織とカンファレンスを 1 枚に、高密度で
3. **なぜこのテーマか + 動機** — **自分の仕事で扱っていて困っていたことが主**。
   写真は右カラムに小さく、GSoC は下段に小さく注記
4. **技術:計画** — 参考実装があり、その lwIP 呼び出しを NuttX の API に置き換える
5. **技術:大枠** — 3 層に分けたときの移植コスト
6. **技術:詳細** — `wg0` の実体、データパス、OS フック 4 関数
7. **デモ** — 実機の動画。トンネル越しの telnet と Web サーバ
8. **（これから）** — 未確定。[残り](#残り未確定) 参照

> **順序の原則:** テーマを選んだ理由は**仕事上の困りごと**であって、GSoC のテーマ一覧で
> 見つけたことではない。GSoC は出自の事実として添えるだけ、落選はおまけ扱いで小さく書く。

## 型の階層

セクションラベル（SUMMARY / WHY THIS TOPIC など）が**主役**。従来は小さな
アイブロウだったが、これを見出しとして大きく見せ、説明の一文を副題に落とす。

| | 役割 | 見た目 |
|---|---|---|
| `.eyebrow` | セクションラベル = **主見出し** | 38px / IBM Plex Mono / 大文字 / `--wg` |
| `h2` | 説明の一文 = 副題 | 28px / Inter 700 / `--muted`。下に本文幅の罫線 1 本 |

タイトルスライドの `.eyebrow`（"Sechack365 Returns"）だけは例外で 13px のまま。

## Slide Structure

### 導入（4 枚 = 約 4 分半）

| # | ラベル（主見出し） | 副題 | Role | Timing |
|---|---|---|---|---|
| 1 | — | — | タイトル | 20 sec |
| 2 | **SUMMARY** | This project implements WireGuard as a NuttX network device. | 作ったもの + WireGuard の説明 + 構成図 | 80 sec |
| 3 | **THE ASF AND COMMUNITY OVER CODE** | Apache NuttX is an ASF project. | 組織 + カンファレンスを 1 枚に | 70 sec |
| 4 | **WHY THIS TOPIC** | I wanted secure remote access to the NuttX devices I work with. | 現場 2 つ（写真は右に小さく）+ 動機 + GSoC（注記） | 90 sec |

### 技術（3 枚 = 約 3 分半）

浅い順に 3 枚。**計画 → 大枠 → 詳細**で、深さが一段ずつ増える。

| # | ラベル（主見出し） | 副題 | Role | Timing |
|---|---|---|---|---|
| 5 | **THE PLAN** | Port an existing implementation: swap its lwIP calls for NuttX ones. | プロポーザルに書いた計画そのまま | 70 sec |
| 6 | **HOW IT WAS BUILT** | Keep the protocol core, replace the network glue. | 大枠：3 層と移植コスト | 70 sec |
| 7 | **THE DETAILS** | `wg0` is a netdev registered with `netdev_register()`, and its wire is a UDP socket. | 詳細：データパス + OS フック 4 関数 | 80 sec |

### デモ（1 枚 = 約 1 分）

| # | ラベル（主見出し） | 副題 | Role | Timing |
|---|---|---|---|---|
| 8 | **THE DEMO** | telnet and a web server, both through the tunnel. | 実機デモ動画のサムネイルと URL | 60 sec |

### 残り（未確定）

ここまでで **約 9 分**。現状は 8 枚目のデモでほぼ回収できているが、upstream の話
（`apache/nuttx-apps` に出すまでに何が残っているか）はまだ入っていない。時間調整は
発表者側で行う前提なので、ここでは削らずに置いてある。

- Make it available to NuttX developers immediately.（upstream / 締め）

---

## スライドごとの中身と話すこと

出典のないものは書かない。各項目の末尾に出典を付けてある。

### 1 — タイトル

**載せるもの**

- `WireGuard for Apache NuttX`
- 副題: A WireGuard VPN implementation for Apache NuttX, exposed as a `wg0` network device.
  — README.md 冒頭
- 右カラムに 3 タイル: Apache NuttX ロゴ / AITRIOS edge AI device (ESP32) / SPRESENSE
- 下部の実績ストリップ 4 項目

| | |
|---|---|
| **ESP32-S3** | TUNNEL UP OVER REAL WI-FI |
| **SPRESENSE** | WG0 UP — TRAFFIC STILL TO VERIFY |
| **Real WireGuard peers** | LINUX KERNEL, WINDOWS CLIENT |
| **Runtime configuration** | SURVIVES A POWER CYCLE |

> **判断の記録:** ストリップから **260 KiB/s** と **4 architectures** を意図的に外した。
> 260 KiB/s は聴衆に比較基準がなく速いか遅いか判断できない（Q&A で答える種類の数字）。
> 4 architectures は 6 枚目の落ちなので、扉で先に出すと重複してネタバレになる。
> 代わりに扉では「どこまで本当に動いているか」を言う。SPRESENSE は動確中であることを
> 隠さず TRAFFIC STILL TO VERIFY と書く（README.ja.md「検証済みの環境」）。

**話すこと**

- 今日は Apache NuttX に WireGuard を実装した話をします
- `wg0` というネットワークデバイスとして見える形で実装しました
- ESP32-S3 の実機では、実際の Wi-Fi 越しにトンネルが張れています
- SPRESENSE でも `wg0` はコード変更なしで起動します。ただしメインボードに Wi-Fi が無いので、
  通信そのものはまだ確認できていません
- 通信相手は常に**本物の** WireGuard 実装 — Linux のカーネルモジュールと Windows 公式
  クライアント。自作実装同士で通信しても相互運用性の証明にならないので

### 2 — SUMMARY

**載せるもの**

WireGuard とは — 4 枚のカード

| ラベル | 中身 | 出典 |
|---|---|---|
| WHAT IT IS | A modern, lightweight VPN protocol, originally developed for Linux. | README.md |
| WHO MADE IT | Jason A. Donenfeld, an independent security researcher. First released in 2016; in the Linux kernel since 5.6 (2020). | wireguard.com（著作権表記 2015-、ZX2C4 / Edge Security）、The Register、proposal.ja.md |
| HOW IT PROTECTS TRAFFIC | Encrypted tunnels over UDP: Curve25519, ChaCha20-Poly1305, BLAKE2s. | README.md |
| WHERE YOU HAVE MET IT | Tailscale is built on WireGuard. Roughly 4,000 lines — small enough for a microcontroller. | Tailscale は口述、4,000 行は README.md |

**図:** README.ja.md「アーキテクチャ」の構成図を SVG で作図済み。
アプリケーション / NSH → NuttX ネットワークスタック（BSD socket API）→ `eth0 / wlan0` と
並んで `wg0`（WireGuard netdev）→ UDP ソケット（ポート 51820）→ インターネット / LTE /
衛星回線 → WireGuard ピア（Linux, Windows, ...）。

**話すこと**

- まず、このリポジトリに何が入っているか
- WireGuard は Linux 向けに開発された軽量な VPN プロトコルで、組み込み・IoT 分野でも採用が
  広がっています
- UDP 上で暗号化トンネルを張り、暗号アルゴリズムは Curve25519・ChaCha20-Poly1305・BLAKE2s
- 実装が約 4,000 行とコンパクトなので、マイコンにも載ります
- **意外と新しくて、大企業のプロダクトでもありません。** 作者は Jason A. Donenfeld という
  独立したセキュリティ研究者で、2015 年に開発を始めて 2016 年に公開しています
- 4 年間は out-of-tree のモジュールとして使われていて、**Linux カーネルにマージされたのは
  2020 年 3 月の 5.6** です
- そして、**Tailscale を使っている人はすでに WireGuard を使っています。**
  Tailscale はこのプロトコルの上に作られているので
- Apache NuttX は POSIX 準拠の RTOS で、独自の TCP/IP スタックと BSD ソケット API を持ちますが、
  **VPN 機能がありません**
- そこで WireGuard を NuttX のネットワークデバイス `wg0` として実装しました。
  アプリから見れば普通のネットワークインターフェースです
- 暗号化された中身を運んでいるのは UDP ポート 51820 のソケットで、通信相手は既存の
  WireGuard エンドポイントで構いません

### 3 — THE ASF AND COMMUNITY OVER CODE

1 枚に 2 列。左が組織、右がカンファレンス。それぞれ公式ロゴを載せる。

**左：Apache Software Foundation**（<https://www.apache.org/foundation/>）

- **The Apache Software Foundation (ASF) exists to provide software for the public good.**
- **a 501(c)(3) nonprofit organization**。1999 年設立
- **run almost exclusively by volunteers that provide support for hundreds of projects**
- Apache License 2.0 — *one of the most relied-upon open source licenses in the world*
- 行動原理は **"The Apache Way"**。その中心が **community over code**（カンファレンス名の由来）

**右：Community Over Code**（<https://communityovercode.apache.org/>）

- **The official conference of the Apache Software Foundation (ASF), bringing together the
  communities behind Apache open source projects.**
- ASF プロジェクトの開発者・利用者・コントリビューター・コミッターのための年次カンファレンス
  シリーズ
- **Glasgow: 10 月 11〜14 日**、Sydney: 11 月 18〜19 日

**下段のまとめ**

- **NuttX International Workshop** が Glasgow 2026 に co-located で開かれる。そこで発表する
  （CFP 提出済み）— README.ja.md「発表」/ README.md "Presentation"
- ゴールは `apache/nuttx-apps` への upstream マージ — README.ja.md「残作業」/
  [docs/upstream/upstream-strategy.md](../../upstream/upstream-strategy.md)

**ロゴ:** [assets/asf-logo-wide.svg](../assets/asf-logo-wide.svg) /
[assets/community-over-code-logo.svg](../assets/community-over-code-logo.svg)
（ASF Brand Guidelines の公式ロゴ。<https://www.apache.org/foundation/press/kit/>）

> 旧 ApacheCon の後継である、という説明は公式サイト上で確認できなかったため書いていない。

**話すこと**

- 技術の話に入る前に、この活動がどこに向かっているかを少しだけ
- Apache Software Foundation は 1999 年設立の 501(c)(3) 非営利団体で、
  「公共の利益のためにソフトウェアを提供する」ために存在しています
- ほぼボランティアだけで運営されていて、数百のプロジェクトを支えています。
  Apache License 2.0 を管理しているのもここです
- **Apache NuttX もその数百のうちの 1 つ**で、このコードを `apache/nuttx-apps` に
  マージしてもらうのがゴールです
- Community Over Code は ASF の公式カンファレンスで、ASF プロジェクトの開発者・利用者・
  コントリビューター・コミッターのための年次イベントです
- 2026 年は 10 月 11〜14 日にグラスゴー、11 月にシドニーで開催されます
- グラスゴーには **NuttX International Workshop** が併設されていて、そこに応募しました
- カンファレンスの名前そのものが ASF の理念です。"The Apache Way" の中心にある
  **community over code** — コードよりコミュニティ

### 4 — WHY THIS TOPIC（現場 + 動機）

左に動機、右に現場の写真を小さく、下段に GSoC の注記。1 枚で「なぜこのテーマか」を完結させる。

**左（主）**

- **NuttX には現時点で VPN 機能が存在しない** — README.ja.md「プロジェクト概要」
- NuttX デバイスへの遠隔・安全なアクセスは、多くの分野で未解決の課題
  — README.ja.md「なぜこのプロジェクトが必要か」
- VPN がない場合の現実的な選択肢は、グローバル IP を晒すか、独自プロトコルを作り込むか、
  ベンダーのクラウドに乗るか——どれも嬉しくない。だからこのテーマを選んだ — README.ja.md

**右（写真は小さく）**

| カード | 画像 | キャプション |
|---|---|---|
| AITRIOS | `aitrios-signage.jpg`（横長 1 枚。IMX500 とカメラ実機） | AITRIOS edge AI cameras / ESP32 + NUTTX |
| SPRESENSE | `spresense-main-lte-hdr-camera.jpg` + `spresense-satellite.jpg`（2 枚並び） | SPRESENSE / A BOTTOM-UP SATELLITE PROJECT |

- どちらも NuttX を**アプリケーション側から使う**立場だった — README.md "About"
- SPRESENSE は Sony の小型・低消費電力マイコンボードで、人工衛星や海洋モニタリングなど
  ミッションクリティカルな用途での採用実績がある — proposal.md

> AITRIOS の画像は日本語のマーケティングコピーが焼き込まれている（「世界初のインテリジェント
> ビジョンセンサー」）。英語スライドの中で 1 枚だけ日本語が入り、カードサイズでは `IMX500`
> 以外読めないが、エッジ AI デバイスが何かは絵で伝わるのでこのまま採用した。

**下段（小さく、注記として）**

- テーマ自体は Google Summer of Code のテーマ一覧で見つけた
- **そのプロポーザルは不採択。** それでも作業は続けた — CLAUDE.md / README.ja.md「現状」

> 注意: このプロジェクトを「GSoC のプロジェクト」として紹介しない（[CLAUDE.md](../../../CLAUDE.md)）。
> 「テーマを見つけた場所が GSoC の一覧だった」という出自の話にとどめ、主題は仕事上の困りごとに置く。

> 要確認 1: 人工衛星プロジェクトの正式名称。提供された画像が **ARICA（AGU Remote Innovative
> CubeSat Alert system）** のものだったため、これが当該プロジェクトである可能性が高い。
> そうであれば、キャプションを "A BOTTOM-UP SATELLITE PROJECT" から **"ARICA CUBESAT"** に
> 差し替えたほうが具体的で強い。確認してから変更する。
>
> 要確認 2: **CubeSat 画像の利用許諾。** 他の画像（Sony 製品写真、ASF 公式ロゴ）と違い、
> `spresense-satellite.jpg` は academist のプロジェクトページ由来の第三者画像で、ライセンスが
> 明示されていない。公開の場で使うなら、権利者（ARICA プロジェクト）の許諾を取るか、
> 自分で撮影・作成した画像に差し替えること。現状はスライド下端に
> "CubeSat: ARICA project" とクレジットを入れてある。**すでに公開リポジトリに push 済み。**

**話すこと**

- なぜこのテーマなのか。理由は一覧ではなく、自分の仕事の側にありました
- ソニーセミコンダクタソリューションズでエッジ AI エンジニアをしていて、NuttX は
  **アプリケーション側から使う**立場です
- 1 つは AITRIOS というエッジ AI プラットフォーム。そこで使っている AI カメラが ESP32 で、
  その OS が NuttX です
- もう 1 つはボトムアップの活動で、SPRESENSE を使った人工衛星のプロジェクト。SPRESENSE は
  ソニーの小型・低消費電力ボードで、衛星や海洋モニタリングなどでの採用実績があります
- **どちらも、リモートから安全に触りたいのに触れない NuttX 機器でした**
- NuttX デバイスへの遠隔・安全なアクセスは多くの分野で未解決の課題ですが、
  NuttX には VPN 機能がありません
- VPN が無いときの選択肢は、グローバル IP を晒すか、独自プロトコルを作り込むか、
  ベンダーのクラウドに乗るか。どれも嬉しくありません
- ちなみに WireGuard は **Tailscale** が使っているプロトコルで、自分は Tailscale の
  ヘビーユーザーでもあります
- WireGuard はコンパクトな実装とシンプルな鍵モデルなので、こうした制約のある環境に向いていて、
  相手は既存の WireGuard エンドポイントで構いません
- （おまけとして小さく）テーマ自体は GSoC のテーマ一覧で見つけたもので、
  **そのプロポーザルは不採択**でした。それでも作業は続けました

### 5 — THE PLAN（計画）

[proposal.md](../../proposal/proposal.md)「3.1 Reference Projects」に書いた計画そのまま。
実装がその通りになっていることをコードで確認済み（`net_driver_s` / `iob_*` / `netdev_register()`）。
唯一ずれたのがソケットで、それは 7 枚目で触れる。

**左：The reference implementation**

- [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) は WireGuard を
  lwIP の `netif` として実装している。`netif` は lwIP の仮想 NIC の抽象で、`eth0` や `wlan0` と
  対等に扱われる — proposal.md
- OS 依存はすべて `wireguard-platform.h` の 4 関数に隔離されている。プロトコルコアと暗号は
  OS 非依存のポータブル C — proposal.md

**右：What had to be replaced**

- NuttX は lwIP を使わない。独自の TCP/IP スタックを持ち、`lwip/netif.h` のような
  lwIP のヘッダは include パスに無い。**`wireguardif.c` はそのままではコンパイルできない**
  — proposal.md

| lwIP | NuttX |
|---|---|
| `struct netif` | `struct net_driver_s` |
| `pbuf_alloc()` | `iob_alloc()` |
| `udp_new()` / `bind()` | BSD `socket()` / `bind()` |

**下段:** これがプロポーザルに書いた計画で、実際の作業もその通りになった。貢献は再利用できる
パターン — lwIP ベースのネットワークコンポーネントを NuttX のネイティブ netdev とソケット API に
つなぐ方法 — proposal.md

**話すこと**

- ここから技術の話です。まず計画から
- **ゼロから WireGuard を書いたわけではありません。** 参考実装があります
- `smartalock/wireguard-lwip` は WireGuard を lwIP の `netif` として実装したものです
- `netif` は lwIP における仮想 NIC の抽象で、`eth0` や `wlan0` と対等に扱われます。
  だから上位のスタックからは普通のインターフェースに見えて、ルーティングもそのまま働きます
- この実装の良いところは、OS 依存の処理が 4 つの関数に隔離されていることです。
  プロトコルのコアと暗号は OS 非依存のポータブル C です
- 問題は、**NuttX が lwIP を使わない**こと。NuttX は独自の TCP/IP スタックを持っていて、
  `lwip/netif.h` のようなヘッダは include パスにすら無い。だから `wireguardif.c` は
  そのままではコンパイルできません
- そこで計画はこうなりました。プラットフォーム層の 4 関数を NuttX 向けに実装し、
  lwIP の API 呼び出しを 1 つずつ NuttX の等価物に置き換える
- `struct netif` は `struct net_driver_s` に、`pbuf_alloc()` は `iob_alloc()` に、
  lwIP の UDP API は BSD ソケットに
- **これがプロポーザルに書いた計画で、実際の作業もその通りになりました**

### 6 — HOW IT WAS BUILT（大枠）

移植元を 3 層に分けて、層ごとの移植コストを示す表 1 枚。README.md「Reference implementations」と
「Source layout」が出典。

| Layer | Porting cost | In this port |
|---|---|---|
| Protocol and crypto（`wireguard.c`, `crypto/`） | None — portable C, used as-is | 3,079 lines, byte-identical to upstream |
| OS hooks（`wireguard-platform.h`） | Low — four functions | 186 lines, written for NuttX |
| lwIP netif glue（`wireguardif.c`） | **Total — replaced** | 2,157 lines（`nuttx-wireguardif.c`） |

> The port moved across four architectures without code changes.
> Isolating the OS hooks is why. x86_64 / ARM Cortex-A7 / Xtensa LX7 / ARM Cortex-M4F.

**クレジット（スライド下端）:** Vendored from smartalock/wireguard-lwip ·
Copyright (c) 2021 Daniel Hope (www.floorsense.nz) · BSD-3-Clause

**話すこと**

- では実際どうだったか。移植元を 3 層に分けると、層ごとの移植コストがまったく違います
- **プロトコルと暗号は移植コストゼロ。** ポータブル C なのでそのまま持ってきました。
  3,079 行、upstream とバイト単位で一致しています
- **OS 依存の 4 関数は低コスト。** NuttX の POSIX API で書いて 186 行
- **lwIP の netif グルーは全損。** NuttX は lwIP を使わないので流用が一切できず、
  ここは書き直しです。それが NuttX 向けに書いた 2,157 行
- そして、**OS 依存をこの小さい 1 ファイルに隔離したことが効きました**。結果として
  4 つのアーキテクチャ — x86_64、ARM Cortex-A7、Xtensa LX7、ARM Cortex-M4F — で
  **コードを 1 行も変えずに動いています**

### 7 — THE DETAILS（詳細）

2 列 + 下段のまとめ。すべて README.ja.md / README.md「アーキテクチャ」と
「The four OS hooks」が出典。

**左：The data path**

- TX: `devif_poll()` → encrypt → `psock_sendto()`
- RX: `psock_poll()` → decrypt → `ipv4_input()`
- 受信はバックグラウンドタスクが `psock_poll()` でブロックする。`drivers/net/tun.c` が手本

**右：The four OS hooks**

| | |
|---|---|
| `wireguard_sys_now()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `wireguard_random_bytes()` | `read("/dev/urandom")` |
| `wireguard_tai64n_now()` | `clock_gettime(CLOCK_REALTIME)` |
| `wireguard_is_under_load()` | `return false` |

**下段（計画からずれた 1 点）:** ソケットはファイルディスクリプタではなく `struct socket` として
保持している。NuttX は fd をタスクグループ単位でスコープするが、送信経路は無関係なワーカー
スレッド上で走るため、5 枚目で「BSD `socket()`」と書いた箇所は実際には `psock_*()` になった。
この都合で現状の実装は FLAT ビルド前提
（[Issue #6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6)）。

**話すこと**

- もう一段細かい話です
- NuttX は独自の TCP/IP スタックを持つので、`wg0` は `netdev_register()` で登録します。
  手本にしたのは NuttX の TUN ドライバ、`drivers/net/tun.c` です
- 下で「配線」の役をしているのは UDP ソケットです
- 送信は `devif_poll()` → 暗号化 → `psock_sendto()`
- 受信はバックグラウンドタスクが `psock_poll()` でブロックして待ち、復号して
  `ipv4_input()` でスタックに注入します
- OS フックの 4 関数はこれだけです。単調増加時刻、暗号乱数、TAI64N タイムスタンプ、
  負荷判定 — 負荷判定は単に false を返します
- 最後に、**計画が生き残らなかった 1 点**。プロポーザルには「BSD `socket()` / `bind()`」と
  書いていましたが、実際にはソケットを fd ではなく `struct socket` として保持し、
  内部 API の `psock_*()` を使うことになりました
- 理由は、NuttX が fd をタスクグループ単位でスコープする一方、送信経路が無関係な
  ワーカースレッド上で走るからです
- この都合で現状の実装は **FLAT ビルド前提**になっていて、これが upstream に出す前の
  未解決の論点の 1 つです

### 8 — THE DEMO（デモ）

**載せるもの**

- 左：デモ動画のサムネイル（`demo-thumb.jpg`）。再生ボタンを重ねてあり、クリックで YouTube が開く
- 右：What the demo shows — 4 行
  - **telnet into the board through the tunnel**, and run commands on it.
  - Start a web server on the board, then open it from a browser.
  - **USB is disconnected.** The board runs on a power adapter alone.
  - The peer is the **official Windows WireGuard client**, over a home Wi-Fi access point.
- 下段：`youtu.be/1kyX2av5WG4` / ESP32-S3 · real Wi-Fi · a real WireGuard peer.

出典は README.ja.md「デモ」（<https://youtu.be/1kyX2av5WG4> — telnet と Web サーバ、
どちらもトンネル越し）、DEVELOPMENT.md、README.md「Verified on」。

> サムネイルは動画そのもののフレーム。左が Windows 公式クライアント（ハンドシェイクと
> 転送量が出ている）、右がトンネル越しの telnet で `uname` / `ifconfig` / `wg show` / `ps` を
> 叩いている画面。**公開鍵は写っているが秘密鍵は写っていない**ので、そのまま使える。

> 本番で動画を流すなら、ネットワークに依存するので**事前にローカルへ落としておく**か、
> このサムネイルのまま口頭で説明して URL を案内する。スライドは後者を前提にしてある。

**話すこと**

- 最後に、実際どう見えるかを
- 左が Windows の公式 WireGuard クライアントです。ハンドシェイクと転送量が出ています
- 右が ESP32-S3 へトンネル越しに入った telnet セッション。`ifconfig` で `wlan0` と `wg0` が
  並んでいて、`wg show` でハンドシェイクが見えて、`ps` でタスクも見えます
- 動画では、トンネル越しに telnet でログインしてコマンドを叩き、ボードの上で Web サーバを
  起動して、最後にブラウザから開いています
- **このあいだ USB は繋がっていません。** 電源アダプタだけで、家庭用の Wi-Fi アクセスポイント
  経由で、相手は Windows の公式クライアントです

---

## 想定質問

| Q | A |
|---|---|
| 速度は | ESP32-S3 実機・実 Wi-Fi 経由でトンネル越し TCP 約 **260 KiB/s** — README.ja.md「現状」 |
| なぜ WireGuard か | 実装規模。約 4,000 行でマイコンに載る。暗号スイート固定でネゴシエーションがない |
| 誰が作ったのか / Google 製? | **違う。** Jason A. Donenfeld（zx2c4）という独立したセキュリティ研究者の個人プロジェクトが出発点。所属は自身の Edge Security。`pass`（Unix のパスワードマネージャ）の作者でもあり、後に Linux カーネルの乱数生成器のメンテナも務めている |
| いつからあるのか | 2015 年開発開始、**2016 年に公開**。4 年間 out-of-tree モジュールとして使われたのち、**2020 年 3 月の Linux 5.6 でマージ**。2018 年に Linus Torvalds が "a work of art" と評したことでも知られる |
| 安定性は | 最長連続動作は **4 時間 28 分**。停止原因は ESP32-S3 の Wi-Fi ドライバ側と特定済み（WireGuard ではない）— README.ja.md |
| 実用になるか | 実行時設定（`wg genkey` / `wg set` / `wg setconf`）・設定の永続化・複数ピアまで動作 |
| 苦労した点 | `ping` は 0% ロスで通るのに TCP のデータだけ届かないバグ。LPWORK ワーカースレッドからの `sendto()` が `EBADF` になっていた — [phase4-log.md](../../development/phase4-log.md) |

---

## スライドを画像で確認する

```bash
python scripts/render-slides.py docs/presentation/returns-slides.html --check-overflow
```

`.slide` は `overflow: hidden` なので、収まらない内容は無言で切り落とされる。
`--check-overflow` を付けて回し、マゼンタの帯が出ないことを確認してから本番に使う。
出力は `renders/returns-slides/slide-NN.png`。詳細は [../../README.md](../../README.md)。

---

## Visual Sources

- Apache NuttX logo: <https://nuttx.apache.org/>
- ASF logo / Community Over Code logo: <https://www.apache.org/foundation/press/kit/>
  (ASF Brand Guidelines. 商標の利用は Identity Style Guide / Trademark Policy に従う)
- SPRESENSE product photo: <https://developer.spresense.sony-semicon.com/>
- AITRIOS edge AI device photos: <https://www.aitrios.sony-semicon.com/edge-ai-devices>
- AITRIOS / IMX500 figure: <https://www.sony.jp/bravia-biz/signage/aitrios/>（`aitrios-signage.jpg`）
- デモ動画サムネイル: <https://www.youtube.com/watch?v=1kyX2av5WG4> の maxresdefault
  （`demo-thumb.jpg`）。自分の動画なので利用に問題はない
- ARICA CubeSat photo: academist のプロジェクトページ由来（`spresense-satellite.jpg`）。
  **ライセンス未確認** — 上の「要確認 2」を参照

Downloaded local copies are in [assets](../assets/).

## Text Policy

- **想像で書かない。** README / proposal にすでにある文面、または口述された内容だけを使う。
  出典のない説明は書かず、「要追記」として空欄にしておく。
- **表現を揃える。** 同じ事柄は README.ja.md / README.md と同じ言い回しで書く。
- キャッチコピー調の一行（「組み込み RTOS に、現代的な VPN を」のような）は入れない。
- Prefer English for slide copy. Avoid awkward Japanese slide copy.
- 「話すこと」は日本語で書く。デッキ内のスピーカーノートは英語のまま残してあるが、
  本番で使うのはこのファイルのほう。
