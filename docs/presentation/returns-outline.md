# Sechack365 Returns talk: WireGuard for Apache NuttX

HTML deck: [returns-slides.html](returns-slides.html)

Audience: Sechack365 Returns alumni event. 持ち時間 **5〜8 分**。話すのは日本語、スライドの
文字は英語（[Text Policy](#text-policy) 参照）。

本編 27 枚のデッキは [slides.html](slides.html)、その台本は [talk-script.md](talk-script.md)。

## Core Message

> A WireGuard VPN implementation for Apache NuttX, exposed as a `wg0` network device.
> Verified on real hardware against real WireGuard peers.

（[README.md](../../README.md) 冒頭 / [README.ja.md](../../README.ja.md)「Apache NuttX 上で動く
WireGuard VPN の実装。`wg0` というネットワークデバイスとして見える。実機で、本物の WireGuard
ピアを相手に検証済み。」）

---

## 話の順番

1. **NuttX に WireGuard を実装した** — 何を作ったか。WireGuard 自体の簡単な説明と図
2. **ASF と Community Over Code とは** — 組織とカンファレンスを 1 枚に、高密度で
3. **なぜこのテーマか + 動機** — 1 枚に統合。**自分の仕事で扱っていて困っていたことが主**。
   写真は右カラムに小さく、GSoC は下段に小さく注記
4. **技術:計画** — 参考実装があり、その lwIP 呼び出しを NuttX の API に置き換える
5. **技術:大枠** — 3 層に分けたときの移植コスト
6. **技術:詳細** — `wg0` の実体、データパス、OS フック 4 関数
7. **（現状・これから）** — 未確定。[残り](#残り未確定) 参照

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

### 残り（未確定）

ここまでで **約 8 分**。現状（実機検証）と upstream の話はまだ入っていない。時間調整は
発表者側で行う前提なので、ここでは削らずに置いてある。

- Verified on real hardware against real WireGuard peers.（現状）
- Make it available to NuttX developers immediately.（upstream / 締め）

---

## スライドごとの中身

出典のないものは書かない。各項目の末尾に出典を付けてある。

### 2 — SUMMARY

**WireGuard とは**（README.ja.md「プロジェクト概要」）

- Linux 向けに開発された軽量な VPN プロトコル。組み込み・IoT 分野でも採用が広がっている
- UDP 上で暗号化トンネルを確立する。暗号アルゴリズムは Curve25519・ChaCha20-Poly1305・BLAKE2s
- 実装がコンパクト（約 4,000 行）で、マイコン上での動作にも適している

**このプロジェクト**（README.ja.md「プロジェクト概要」）

- Apache NuttX は POSIX 準拠の RTOS で、独自の TCP/IP スタックと BSD ソケット API を持つ。
  しかし現時点では VPN 機能が存在しない
- WireGuard を **NuttX のネットワークデバイス（`wg0`）** として実装し、NuttX 機器への
  安全な遠隔アクセスを可能にする
- 参照実装として [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) を使用している

**図:** README.ja.md「アーキテクチャ」の構成図を SVG で作図済み。
アプリケーション / NSH → NuttX ネットワークスタック（BSD socket API）→ `eth0 / wlan0` と
並んで `wg0`（WireGuard netdev）→ UDP ソケット（ポート 51820）→ インターネット / LTE /
衛星回線 → WireGuard ピア（Linux, Windows, ...）。

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
- Apache NuttX もその hundreds of projects のひとつで、ゴールは `apache/nuttx-apps` への
  upstream マージ — README.ja.md「残作業」/
  [docs/upstream/upstream-strategy.md](../upstream/upstream-strategy.md)

**ロゴ:** [assets/asf-logo-wide.svg](assets/asf-logo-wide.svg) /
[assets/community-over-code-logo.svg](assets/community-over-code-logo.svg)
（ASF Brand Guidelines の公式ロゴ。<https://www.apache.org/foundation/press/kit/>）

> 旧 ApacheCon の後継である、という説明は公式サイト上で確認できなかったため書いていない。

### 4 — WHY THIS TOPIC（現場 + 動機）

左に動機、右に現場の写真を小さく、下段に GSoC の注記。1 枚で「なぜこのテーマか」を完結させる。

**左（主）**

- **NuttX には現時点で VPN 機能が存在しない** — README.ja.md「プロジェクト概要」
- NuttX デバイスへの遠隔・安全なアクセスは、多くの分野で未解決の課題
  — README.ja.md「なぜこのプロジェクトが必要か」
- VPN がない場合の現実的な選択肢は、グローバル IP を晒すか、独自プロトコルを作り込むか、
  ベンダーのクラウドに乗るか——どれも嬉しくない。だからこのテーマを選んだ — README.ja.md

**右（写真は小さく、1 カードに 2 枚ずつ）**

- AITRIOS カード: `aitrios-csv26.png` + `aitrios-aih-ivrw2.png`
- SPRESENSE カード: `spresense-main-lte-hdr-camera.jpg` + `spresense-satellite.jpg`（ARICA の CubeSat）


| | 何 | ボード / OS |
|---|---|---|
| **AITRIOS** | エッジ AI プラットフォームの AI カメラ | ESP32 + NuttX |
| **SPRESENSE** | ボトムアップ活動の人工衛星プロジェクト | SPRESENSE |

- どちらも NuttX を**アプリケーション側から使う**立場だった — README.md "About"
- SPRESENSE は Sony の小型・低消費電力マイコンボードで、人工衛星や海洋モニタリングなど
  ミッションクリティカルな用途での採用実績がある — proposal.md

**下段（小さく、注記として）**

- テーマ自体は Google Summer of Code のテーマ一覧で見つけた
- **そのプロポーザルは不採択。** それでも作業は続けた — CLAUDE.md / README.ja.md「現状」

**話で補う:** WireGuard は **Tailscale** で使われているプロトコルで、自分は Tailscale の
ヘビーユーザーだった。WireGuard のコンパクトな実装とシンプルな鍵モデルはこうした制約のある
環境に適しており、相手は既存の WireGuard エンドポイントで構わない（README.ja.md）。

> 注意: このプロジェクトを「GSoC のプロジェクト」として紹介しない（[CLAUDE.md](../../CLAUDE.md)）。
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
> "CubeSat: ARICA project" とクレジットを入れてある。

### 5 — THE PLAN（計画）

[proposal.md](../proposal/proposal.md)「3.1 Reference Projects」に書いた計画そのまま。
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
つなぐ方法 — proposal.md「The contribution of this project is demonstrating and documenting
how to connect a lwIP-based network component to NuttX's native netdev and socket API」

### 6 — HOW IT WAS BUILT（大枠）

移植元 [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip) を 3 層に分けて、
層ごとの移植コストを示す表 1 枚。README.md「Reference implementations」と「Source layout」が出典。

| Layer | Porting cost | In this port |
|---|---|---|
| Protocol and crypto（`wireguard.c`, `crypto/`） | None — portable C, used as-is | 3,079 lines, byte-identical to upstream |
| OS hooks（`wireguard-platform.h`） | Low — four functions | 186 lines, written for NuttX |
| lwIP netif glue（`wireguardif.c`） | **Total — replaced** | 2,157 lines（`nuttx-wireguardif.c`） |

> The port moved across four architectures without code changes.
> Isolating the OS hooks is why. x86_64 / ARM Cortex-A7 / Xtensa LX7 / ARM Cortex-M4F.
> — README.md "Isolating them there is why the port moved across four architectures without code changes."

**クレジット（スライド下端）:** Vendored from smartalock/wireguard-lwip ·
Copyright (c) 2021 Daniel Hope (www.floorsense.nz) · BSD-3-Clause

### 7 — THE DETAILS（詳細）

2 列 + 下段のまとめ。すべて README.ja.md /  README.md「アーキテクチャ」と
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

---

## スライドを画像で確認する

```bash
python scripts/render-slides.py docs/presentation/returns-slides.html --check-overflow
```

`.slide` は `overflow: hidden` なので、収まらない内容は無言で切り落とされる。
`--check-overflow` を付けて回し、マゼンタの帯が出ないことを確認してから本番に使う。
出力は `renders/returns-slides/slide-NN.png`。詳細は [../README.md](../README.md)。

---

## Visual Sources

- Apache NuttX logo: <https://nuttx.apache.org/>
- ASF logo / Community Over Code logo: <https://www.apache.org/foundation/press/kit/>
  (ASF Brand Guidelines. 商標の利用は Identity Style Guide / Trademark Policy に従う)
- SPRESENSE product photo: <https://developer.spresense.sony-semicon.com/>
- ARICA CubeSat photo: academist のプロジェクトページ由来（`spresense-satellite.jpg`）。
  **ライセンス未確認** — 上の「要確認 2」を参照
- AITRIOS edge AI device photos: <https://www.aitrios.sony-semicon.com/edge-ai-devices>

Downloaded local copies are in [assets](assets/).

## Text Policy

- **想像で書かない。** README / proposal にすでにある文面、または口述された内容だけを使う。
  出典のない説明は書かず、「要追記」として空欄にしておく。
- **表現を揃える。** 同じ事柄は README.ja.md / README.md と同じ言い回しで書く。
- キャッチコピー調の一行（「組み込み RTOS に、現代的な VPN を」のような）は入れない。
- Prefer English for slide copy. Avoid awkward Japanese slide copy.
