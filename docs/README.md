# docs

| ディレクトリ | 中身 | いつ読むか |
|---|---|---|
| [design.html](design.html) | 設計ドキュメント（図表ベース） | **まずこれ。** 何をどう作ったかを一枚で |
| [library-usage.html](library-usage.html) | 移植元ライブラリの使い方 | 「何を借りて何を書いたか」を理解したいとき |
| [development/](development/) | 開発と検証の記録 | 実装の経緯・詰まった点・実機で何が起きたかを追うとき |
| [upstream/](upstream/) | `apache/nuttx-apps` への提出準備 | PR を出す前に |
| [presentation/](presentation/) | 発表資料 | 登壇・デモの前に |
| [proposal/](proposal/) | 当初のプロポーザル（記録） | 当時何を計画していたかを確認するとき |
| [releases/](releases/) | リリースノート | タグを打ったときの状態と既知の問題を確認するとき |

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
| [chachapoly-nonce-draft.md](upstream/chachapoly-nonce-draft.md) | `apache/nuttx` への報告ドラフト: `crypto/chachapoly` の u64 nonce が counter を誤った位置に置く(WireGuard で発覚、fork では修正済み・未提出) |
| [in-kernel-status.md](upstream/in-kernel-status.md) | カーネル移行の**実装状況メモ**(2026-09-20: sim 完動・rv-virt:knetnsh64 で実カーネルビルド完全トンネル・残タスクは実機のみ) |
| [in-kernel-plan.md](upstream/in-kernel-plan.md) | **カーネル側移行計画**(`drivers/net/` + ioctl 越しの `wg`)。設計・実装順序・テスト戦略・マージ戦略・3 名の専門家レビュー |
| [in-kernel-review-brief.md](upstream/in-kernel-review-brief.md) | **他 LLM / レビュアーに渡す自己完結ブリーフ**。目的・設計判断・ABI・検証状況・変更規模・番号付きレビュー論点を1枚に(Issue からリンクして相互レビューに使う) |
| [in-kernel-design.md](upstream/in-kernel-design.md) | **実装に対応した設計説明**(実コードから)。アクター・ライフサイクル・データ経路・所有・ロック(net_lock がプロトコル状態を直列化)・ABI 更新規約・時刻/乱数・ビルド種別ごとの検証範囲 |
| [verification-matrix.md](upstream/verification-matrix.md) | **カーネル版の検証マトリクス**(正直版)。計画 §3 の T0〜T8 / TF・TV・TR・TN・TZ・TE・TT を テストID・対象 SHA・構成・結果・ログ・未実施理由に対応づけ。「スクリプトがある」と「テストが通った」を区別 |
| [reproduce.md](upstream/reproduce.md) | **新規 clone からの再現手順**(固定 SHA・コンテナ作成・ツールチェーン・ビルド/テストコマンド・期待結果)。fork 未 push という制約も明示 |
| [review-request.md](upstream/review-request.md) | **相互レビューの依頼テンプレート**。Issue コメント文面 + 他 LLM への投げ文(Web 閲覧可/不可の2種)+ 運用ループ |

いきなり PR を投げず、先に `dev@` で設計の合意を取る方針。

## presentation/

デッキ本体(HTML / PDF)はここ、台本はすべて [presentation/talkscript/](presentation/talkscript/) に
デッキと同じ名前で置く。

| デッキ | 台本 | |
|---|---|---|
| [slides.html](presentation/slides.html) | [talkscript/slides.md](presentation/talkscript/slides.md) | 本編 27 枚。`←→` 送り、`N` で発表者ノート、`O` で一覧。台本は時間配分と削る順番つき |
| [returns-slides.html](presentation/returns-slides.html) / [.pdf](presentation/returns-slides.pdf) | [talkscript/returns-slides.md](presentation/talkscript/returns-slides.md) | Sechack365 Returns 向け 8 枚（公開済み・凍結）。台本側が構成・話すこと・出典の単一の情報源 |
| [short-slides.html](presentation/short-slides.html) / [.pdf](presentation/short-slides.pdf) | [talkscript/short-slides.md](presentation/talkscript/short-slides.md) / [英語版](presentation/talkscript/short-slides-en.md) | 9 枚。Returns 版に Spresense 実機確認と NuttX 3 バージョン（13.0.1 / master / 12.7.0）を足したもの。台本は 10 / 8 / 5 分のルートつき。英語で話す場合は `-en` を使う |
| （共通） | [talkscript/presentation-script.md](presentation/talkscript/presentation-script.md) | 進行表と想定 Q&A |

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

## releases/

| | |
|---|---|
| [v0.1.0.md](releases/v0.1.0.md) | apps 版（FLAT ビルド）の完成。検証環境・入っているもの・制約・既知の問題・次 |
| [v0.1.1.md](releases/v0.1.1.md) | v0.1.0 の既知の問題（replay 順序・cookie・鍵ゼロ化・乱数・routing）の修正と否定系テスト |
