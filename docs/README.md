# docs

| ディレクトリ | 中身 | いつ読むか |
|---|---|---|
| [design.html](design.html) | 設計ドキュメント（図表ベース） | **まずこれ。** 何をどう作ったかを一枚で |
| [library-usage.html](library-usage.html) | 移植元ライブラリの使い方 | 「何を借りて何を書いたか」を理解したいとき |
| [development/](development/) | 開発と検証の記録 | 実装の経緯・詰まった点・実機で何が起きたかを追うとき |
| [upstream/](upstream/) | `apache/nuttx-apps` への提出準備 | PR を出す前に |
| [presentation/](presentation/) | 発表資料 | 登壇・デモの前に |
| [proposal/](proposal/) | 当初のプロポーザル（記録） | 当時何を計画していたかを確認するとき |

リポジトリ全体の現状は [../DEVELOPMENT.md](../DEVELOPMENT.md)、
使い方とビルド方法は [../README.md](../README.md)。

---

## design.html

図と表だけで設計を説明する単一のドキュメント。データ経路、`wg0` を netdev にした判断、
fd がタスクグループにスコープされる話、実測値。スライドに貼れる粒度の図を持たせてある。

## development/

| | |
|---|---|
| [dev-environment.md](development/dev-environment.md) | sim / QEMU / 実機の使い分け |
| [phase1-log.md](development/phase1-log.md) | ビルドシステム統合 |
| [phase2-log.md](development/phase2-log.md) | プラットフォーム層・netdev 統合 |
| [phase3-log.md](development/phase3-log.md) | 実ハンドシェイク検証。`SO_RCVTIMEO` と detached pthread の件 |
| [phase4-log.md](development/phase4-log.md) | ESP32-S3 実機。TCP が通らないバグの調査、Wi-Fi ドライバのクラッシュ解析、他ボードの試行 |
| [phase4-summary.md](development/phase4-summary.md) | Phase 4 の成果まとめ・デモ動画 |
| [hardware-verification.md](development/hardware-verification.md) | 各ボードで何ができて何ができなかったか |
| [code-review-2026-08.md](development/code-review-2026-08.md) | コード全体のレビューと課題の棚卸し |
| [logs-esp32s3-wifi-crash.txt](development/logs-esp32s3-wifi-crash.txt) | 4h28m 稼働後のクラッシュダンプ（生ログ） |

失敗も残してある。うまくいった手順だけの記録は、次に同じ道を通る人の役に立たない。

## upstream/

| | |
|---|---|
| [upstream-strategy.md](upstream/upstream-strategy.md) | 提出計画・PR の分割方針・論点 |
| [dev-list-proposal.md](upstream/dev-list-proposal.md) | `dev@nuttx.apache.org` への投稿ドラフト |
| [license-appendix-draft.md](upstream/license-appendix-draft.md) | `LICENSE` に追記する著作権表示の案 |
| [gs2200m-usrsock-issue-draft.md](upstream/gs2200m-usrsock-issue-draft.md) | `apache/nuttx` への報告ドラフト: GS2200M usrsock の ioctl 2 件(未提出) |
| [rtc-hires-wdog-regression-draft.md](upstream/rtc-hires-wdog-regression-draft.md) | `apache/nuttx` への報告ドラフト: master の `CONFIG_RTC_HIRES` 起動回帰(cxd56、未提出) |

いきなり PR を投げず、先に `dev@` で設計の合意を取る方針。

## presentation/

| | |
|---|---|
| [slides.html](presentation/slides.html) | 発表スライド 27 枚。`←→` 送り、`N` で発表者ノート、`O` で一覧 |
| [talk-script.md](presentation/talk-script.md) | 読み上げ台本。時間配分と、枠に合わせて削る順番つき |
| [short-slides.html](presentation/short-slides.html) | 紹介デッキ 9 枚。Returns 版に Spresense 実機確認と NuttX バージョン（12.7.0 / master）のスライドを足したもの。`←→` 送り、`N` で発表者ノート |
| [short-summary.md](presentation/short-summary.md) | その原稿（構成・時間配分・話で補う内容） |
| [presentation-script.md](presentation/presentation-script.md) | 進行表と想定 Q&A |
| [returns-slides.html](presentation/returns-slides.html) | Sechack365 Returns 向け 8 枚（公開済み・凍結）。`←→` 送り、`N` で発表者ノート |
| [returns-slides.pdf](presentation/returns-slides.pdf) | その PDF 版（`render-slides.py` の 2 倍解像度 PNG から生成） |
| [returns-outline.md](presentation/returns-outline.md) | その構成（スライドごとの中身と出典、文面の方針） |

### スライドを画像にする

デッキは 1280x720 の `#canvas` に 1 枚ずつ `<section class="slide">` を置いた単一 HTML で、
`deck.html#3` のようにフラグメントでスライドを指定できる。これをヘッドレス Chrome で
1 枚ずつ PNG にするのが [../scripts/render-slides.py](../scripts/render-slides.py)。

```bash
python scripts/render-slides.py docs/presentation/returns-slides.html
python scripts/render-slides.py docs/presentation/slides.html --slides 1-5,9
python scripts/render-slides.py <deck> --check-overflow      # はみ出しを可視化
```

出力は `docs/presentation/renders/<デッキ名>/slide-NN.png`（既定で 2 倍解像度 = 2560x1440）。
`.slide` は `overflow: hidden` なので、収まらない内容は**何も言わずに切り落とされる**。
`--check-overflow` を付けると、はみ出したスライドにマゼンタの帯と超過ピクセル数が描かれるので、
レビュー時はこれを付けて回すとよい。生成物は `.gitignore` 済み。

> **収録・登壇時の注意:** `.config`・`kconfig-tweak` の実行画面・ビルドログを画面に出さないこと
> （Wi-Fi の SSID とパスフレーズが平文で入っている）。`wg showconf` は秘密鍵を表示する。

## proposal/

GSoC 2026 への応募文書。採択されなかったが、当時の計画と見積もりの記録として残してある。
現状とは食い違うので、いま何が動くかは [../README.md](../README.md) を参照。

| | |
|---|---|
| [proposal.md](proposal/proposal.md) / [proposal.ja.md](proposal/proposal.ja.md) | プロポーザル本体 |
| [proposal-asf2026.md](proposal/proposal-asf2026.md) | ASF カンファレンス CFP 用 |
