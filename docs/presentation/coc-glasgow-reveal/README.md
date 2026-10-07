# CoC Glasgow 発表 — reveal.js 版（公式テンプレート適用）

Community Over Code Glasgow の公式スライドテンプレート
（[raboof/community-over-code-glasgow-template](https://codeberg.org/raboof/community-over-code-glasgow-template)、
reveal.js 6.0.2 ベース、取得コミット `5602ce54`）の `cocglasgow` テーマを、
本編デッキ [../coc-glasgow-slides.html](../coc-glasgow-slides.html) に適用したもの。

**スライドの内容の正本は `../coc-glasgow-slides.html`（A 版）のまま**。この版（B 版）は
そこから `build.py` で生成する。A 版は単体で動くフォールバックとして手を加えずに残す。

## 使い方

`index.html` をブラウザで開く（`file://` のままで動く。ネット接続なしでも
Plex Mono 以外のフォントは同梱品で表示される）。

| キー | 動作 |
|---|---|
| `←` `→` / `Space` | 送り・戻し |
| `S` | 発表者ビュー（ノート・次スライド・タイマー）を別ウィンドウで開く |
| `F` | 全画面 |
| `Esc` / `O` | 一覧 |

PDF にする場合は reveal.js 標準の手順で、`index.html?print-pdf` を Chrome で開き、
印刷 → PDF に保存（余白なし・背景グラフィックあり）。**未確認**: ヘッドレス Chrome では
この版でもテンプレート原本でも印刷レイアウトが起動せず白紙になり、通常の Chrome での
確認はまだしていない。画像が要るだけなら `check.py --shots` の PNG を使う。

## 更新の手順

```bash
cd docs/presentation/coc-glasgow-reveal
python build.py      # ../coc-glasgow-slides.html から index.html を生成
python check.py      # 全スライドのはみ出し検査（ヘッドレス Chrome / Edge）
python check.py --shots <dir>   # 1 枚ずつ PNG も保存
```

`check.py` は、要素がフレーム外・CoC フッター帯の下・出典行（`.sources`）の上に
はみ出したスライドを報告し、1 件でもあれば終了コード 1 を返す。Web フォントの
読み込み完了後に測るので、Open Sans の幅で判定される。

| ファイル | 役割 | 編集してよいか |
|---|---|---|
| `index.html` | 生成物 | **手で編集しない**（`build.py` で上書きされる） |
| `build.py` | A 版から本文・CSS・ノートを持ってくる。表紙と最終スライドはここに直書き | はい |
| `template.html` | reveal.js の外枠と初期化、`?check` 用の検査スクリプト | はい |
| `frame-reset.css` | reveal.js / テーマの要素スタイルを `.frame` 内で打ち消す | 通常は不要 |
| `coc.css` | CoC の見た目（見出し・フッター・アクセント・表紙） | はい |
| `vendor/` | reveal.js 本体・テーマ・notes プラグイン（MIT）、Open Sans（OFL） | いいえ |
| `img/` | テンプレート同梱の表紙イラスト・フッターロゴ | いいえ |

## A 版との違い

- 見出し（eyebrow）が Montserrat Bold・紫 `#7c297d`、本文が Open Sans
- 全スライド下端に CoC のグラデーションフッター（日付・ロゴ・開催地）
- 表紙と「Thank you」はテンプレートの濃紫背景＋グラスゴーの街並み
- 区切り線・Takeaway・Agenda の番号など**装飾としての赤**は CoC の配色に変更。
  図の中の WireGuard 赤・NuttX 緑・青・金は意味を持つので変えていない
- 発表者ノートは reveal.js の `<aside class="notes">` に変換済み（`S` キーで表示）
