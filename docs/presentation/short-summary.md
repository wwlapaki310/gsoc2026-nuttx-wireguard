# 5分紹介スライド構成: WireGuard を Apache NuttX へ

短時間の紹介枠・LT 用。**6 枚 / 5 分**。
この原稿から起こしたデッキ: [short-slides.html](short-slides.html)
1 枚 = 見出し + 主役の要素ひとつ + 締めの一行。詰め込まない。

本編の 27 枚デッキは [slides.html](slides.html)、その台本は [talk-script.md](talk-script.md)。

| 枚 | 見出し | 主役 | 目安 |
|---|---|---|---|
| 1 | **Title** — WireGuard Port to Apache NuttX | 実績バー | 10 秒 |
| 2 | **Summary** — NuttX に VPN がなかった | 従来／今回の対比表 | 60 秒 |
| 3 | **Motivation** — 現場に置いたあと、触れない | 2 つの現場の表 | 100 秒 |
| 4 | **Implementation** — 4 関数だけ置き換える | 移植内訳の表 | 60 秒 |
| 5 | **Current Status** — ESP32-S3 実機で動作 | デモ経路の模式図 | 60 秒 |
| 6 | **Next Steps** — これから | 3 項目 | 30 秒 |

削るなら 4。**3 は削らない**（この活動が単なる移植ではない理由）。

---

## 1 — Title

### WireGuard Port to Apache NuttX

<sub>proposal.md のタイトルと同一にしてある</sub>

組み込み RTOS に、現代的な VPN を

秋田 悟 / ソニーセミコンダクタソリューションズ

**下部バー**

| ESP32-S3 実機 | 実 Wi-Fi | Windows 公式クライアント | 260 KiB/s |
|---|---|---|---|

---

## 2 — Summary: NuttX に VPN がなかった

| | 従来 | 今回 |
|---|---|---|
| 接続 | シリアルケーブル | **ネットワーク** |
| 場所 | 機器の前 | **リモート** |
| 経路 | 平文 | **暗号化トンネル** |
| 道具 | 専用 | `ssh` / `telnet` / HTTP **そのまま** |

> `wg0` という仮想 NIC が生える。アプリから見れば、ただのネットワークインターフェース。

**図案:** 上下 2 段。上「PC ─ シリアル ─ ボード」、下「PC ─ インターネット ─ 暗号化トンネル ─ ボード」

---

## 3 — Motivation: 現場に置いたあと、触れない

| | 何 | なぜ触れない |
|---|---|---|
| **AITRIOS** | エッジ AI プラットフォームの AI カメラ<br>**ESP32 + NuttX** | 天井・屋外の高所。<br>異常の兆候だけで人は出せない |
| **人工衛星** | ボトムアップ活動<br>**SPRESENSE** | 打ち上げ後は<br>物理アクセス不可 |

> ### ネットワークが唯一のメンテナンス経路。
> 本来は VPN で解決する話。**NuttX にはその選択肢がなかった。**

**話で補う（スライドに書かない）:** きっかけは GSoC のテーマ一覧。
VPN がない場合の代替は、グローバル IP を晒す／独自プロトコルを作る／ベンダークラウドに乗る。
WireGuard は約 4,000 行なのでマイコンに載る。

---

## 4 — Implementation: 4 関数だけ置き換える

**移植元:** [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip)
Copyright (c) 2021 Daniel Hope (www.floorsense.nz) / BSD-3-Clause
— lwIP 向けの WireGuard 実装。**OS 依存が 4 関数に隔離されている**

| | 扱い | 規模 |
|---|---|---|
| プロトコル・暗号 | **無改変で流用** | 約 3,000 行 |
| OS 依存の 4 関数 | NuttX の POSIX API で実装 | 186 行 |
| lwIP netif グルー | **NuttX 用に書き直し** | 約 2,700 行 |

> ### 結果: 4 アーキテクチャで**コード変更ゼロ**
> x86_64 / Cortex-A7 / Xtensa LX7 / Cortex-M4F

**話で補う:** NuttX は lwIP ではなく独自スタックなので netif は流用できない。
`wg0` は `netdev_register()` で登録する仮想 NIC で、その「配線」は UDP ソケット。
OS 固有の勘所は [ciniml/WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino)
（同じコードの ESP32 / FreeRTOS + ESP-IDF への移植）を先例として参照した。

---

## 5 — Current Status: ESP32-S3 実機で動作

**主役:** デモのスクリーンショット（ブラウザで `http://10.10.0.2/` が開いている画面）

動画: <https://youtu.be/1kyX2av5WG4>

- トンネル越しに telnet → ボード上で Web サーバ起動 → ブラウザで閲覧
- **USB 未接続。電源アダプタのみ**
- 相手は Windows 公式クライアント = **仕様側の実装との相互運用**

| 並行して | 状態 |
|---|---|
| Raspberry Pi Pico 2 W | 挑戦中（CYW43439 ドライバが RP2350 側に未整備） |
| SPRESENSE | `wg0` 起動まで確認済み。Wi-Fi モジュール確保後に通信検証 |

---

## 6 — Next Steps: これから

- **対応実機を広げる**
- **Community Over Code グラスゴー（10 月）** — インパクトのあるデモの形を検討
- **upstream へマージ**

---
---

## 補足: 想定質問

| Q | A |
|---|---|
| 速度は | ESP32-S3 実機・実 Wi-Fi 経由でトンネル越し TCP 約 **260 KiB/s** |
| なぜ WireGuard か | 実装規模。OpenVPN / IPsec は十万行以上、WireGuard は約 4,000 行。暗号スイート固定でネゴシエーションがない |
| 苦労した点は | 「テストは通るが壊れている」の繰り返し。最悪は **`ping` は 0% ロスで通るのに TCP のデータだけ 1 バイトも届かない**（詳細は本編） |
| 実用になるか | 実行時設定・設定の永続化・複数ピアまで動作。最長連続動作は 4 時間 28 分で、停止原因は Wi-Fi ドライバ側と特定済み |

---

## 参考

| | |
|---|---|
| **WireGuard** | <https://www.wireguard.com/> — プロトコル本体。Linux カーネル 5.6 でマージ |
| **Apache NuttX** | <https://nuttx.apache.org/> — POSIX 準拠の組み込み RTOS |
| **smartalock/wireguard-lwip** | <https://github.com/smartalock/wireguard-lwip> — 移植元。Copyright (c) 2021 Daniel Hope (www.floorsense.nz)、BSD-3-Clause |
| **ciniml/WireGuard-ESP32-Arduino** | <https://github.com/ciniml/WireGuard-ESP32-Arduino> — 同じコードの ESP32（FreeRTOS + ESP-IDF）への移植。OS 差分の先例として参照 |
| **本プロジェクト** | <https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard> |
| **NuttX 側の議論** | <https://github.com/apache/nuttx/issues/18548> |
| **AITRIOS** | <https://www.aitrios.sony-semicon.com/> — ソニーセミコンダクタソリューションズのエッジ AI プラットフォーム |
| **SPRESENSE** | <https://developer.sony.com/spresense/> |
| **Community Over Code** | <https://communityovercode.org/> — 2026 年 10 月 11〜14 日、グラスゴー |
