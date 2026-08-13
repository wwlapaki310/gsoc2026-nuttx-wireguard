# upstream (apache/nuttx-apps) マージ戦略

## この文書の位置づけ

`apps/netutils/wireguard/` を最終的に `apache/nuttx-apps` にマージすることを見据え、NuttX 公式の Contributing Guide・Inviolables・C Coding Standard を実際に確認した上で、現状のコードとのギャップと段階的な進め方をまとめたもの。参照した一次情報:

- [CONTRIBUTING.md](https://github.com/apache/nuttx/blob/master/CONTRIBUTING.md)(PR/コミット規約)
- [INVIOLABLES.md](https://github.com/apache/nuttx/blob/master/INVIOLABLES.md)(不可侵の原則: ライセンス・コーディングスタイル等)
- [C Coding Standard](https://nuttx.apache.org/docs/latest/contributing/coding_style.html)(ファイルヘッダ・関数コメントのテンプレート)
- [apache/nuttx-apps LICENSE](https://github.com/apache/nuttx-apps/blob/master/LICENSE)(サードパーティ BSD/MIT コード同梱時の既存の書き方)

すべて英語の一次情報なので、以下は「読んだ上でのまとめ」であり、実際に PR を出す前にメンター(Alan Carvalho de Assis)と最新版を再確認すること。

---

## 1. 確認できた必須要件

### 1.1 PR は小さく、機能単位を1つに絞る

> "Pull Requests should be as small as possible and focused on only one functional change."

これまでの開発(Phase 1〜4 前倒し分)を1本の巨大 PR にまとめて出すのは NG。フェーズ単位、あるいはそれよりさらに細かい単位に分割する必要がある(詳細は §3)。

### 1.2 実機テストログが必須

> "For code changes build and runtime logs are **mandatory** to prove code was tested on **at least one** real world hardware target."

これが今のプロジェクト進捗における一番大きいギャップ。現状の検証は sim / QEMU のみで、**実機(ESP32-S3)でのビルド・実行ログがない状態では通常の PR ですら受理されない**。[hardware-verification.md](hardware-verification.md) の手順を実施し、ログを取得することが upstream 提出の前提条件になる。

Breaking Change 扱いの変更はさらに厳しく「QEMU のテストはカウントされない」「複数アーキテクチャでの実機ログが必須」と明記されている。今回の変更はどのアーキテクチャの既存動作も変えない(新規 Kconfig はデフォルト `n`)ため Breaking Change には該当しない想定だが、念のため意識しておく。

### 1.3 コミットメッセージ・PR の書式

トピック行 + 空行 + 説明 + 空行 + `Signed-off-by:`(`git commit -s`)という DCO 形式。加えて重要な一点:

> "The `Assisted-by:` field is required to be present for any and all commits in which generative tooling/AI tools were used [...] `Assisted-by: AGENT_NAME:MODEL_VERSION [TOOL1] [TOOL2]`"
>
> "AI agents **MUST NOT** add `Signed-off-by` tags. Only humans can legally certify the commit."

**このリポジトリのコードはほぼ全て Claude Code (Claude Sonnet 5) が実装したもの。** upstream に出す全コミットに `Assisted-by: Claude:claude-sonnet-5` のようなタグを付ける必要がある(`Signed-off-by` は人間である利用者自身が付ける)。これは隠す/省略できる項目ではなく ASF の生成 AI ポリシーに基づく必須項目なので、最初から運用に組み込む。

PR タイトルも `functional/area: 内容` 形式(例: `netutils/wireguard: Add wg0 netdev registration.`)。

### 1.4 コーディングスタイル (`checkpatch.sh` / `nxstyle`)

```bash
./tools/checkpatch.sh -c -u -m -g HEAD~...HEAD      # コミット範囲チェック
./tools/checkpatch.sh -c -u -m -f path/to/file.c    # 単一ファイルチェック
```

ファイルヘッダは以下の形式が必須(コーディング規約 Appendix より抜粋):

```c
/****************************************************************************
 * <ファイルの相対パス>
 * <1行の説明(任意)>
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  ...
 *
 ****************************************************************************/
```

加えてファイル内は `Included Files` / `Pre-processor Definitions` / `Private Types` / `Private Function Prototypes` / `Private Data` / `Public Data` / `Private Functions` / `Public Functions` の区切りコメントで整理し、各関数に `Name:` / `Description:` / `Input Parameters:` / `Returned Value:` のヘッダコメントを付ける。

**現状ギャップ:** `nuttx-platform.c` / `nuttx-wireguardif.c` / `nuttx-wireguardif.h` / `wg_main.c` は `drivers/net/tun.c` を参考にした簡易なヘッダ・コメントで書いており、上記テンプレートに完全準拠していない。PR 前に `checkpatch.sh` を実際に走らせて機械的に直す。

### 1.5 レビュー要件

> "2 or more independent positive reviews." / "Self review is not allowed." / "When code comes from the same organization as positive review, then review is not considered independent."

メンターのレビューだけでは足りない可能性がある(所属が近い場合は「独立したレビュー」と見なされないことがある)。dev@nuttx.apache.org での早期共有を通じて、もう1名以上のレビュアーを確保する動きが必要。

### 1.6 ライセンス

- INVIOLABLES.md: NuttX が許容するのは BSD 3条項互換・MIT・Public Domain 等の「制約のない」ライセンス。**wireguard-lwip の BSD-3-Clause はそのまま適合する**(現状 `ALLOW_BSD_COMPONENTS` ゲートで対応済み — この設計判断は正しかった)。
- コーディング規約: 「サードパーティファイルの場合、そのファイル自体は元のライセンス表記を保持し、Apache 2.0 に置き換えない。その代わり、トップレベルの `LICENSE` および/または `NOTICE` ファイルに差分を記載する」
- `apache/nuttx-apps/LICENSE` を実際に確認すると、uIP・FreeModbus・THTTPD・PPPD・lvgl などの BSD/MIT コードについて、**同ファイル内の Appendix に元の著作権表示をそのまま引用する形で列挙されている**。wireguard-lwip (`Copyright (c) 2021 Daniel Hope (www.floorsense.nz)`) も同じパターンで `LICENSE` に追記が必要。

**現状ギャップ:** `wireguard.c` / `crypto/refc/*.c` などの vendored ファイルは Phase 1 の段階で正しく元の BSD ヘッダを保持したままコピーされている(良い状態)。ただし `apache/nuttx-apps/LICENSE` への追記はまだ行っていない — これは PR の一部として必須。

---

## 2. 現状コードの upstream 化ギャップまとめ

| 項目 | 現状 | 必要な対応 |
|---|---|---|
| ビルド方式 | Docker が `wireguard-lwip` を都度 `git clone` してコピーし、`nuttx_port/` を上書き | upstream PR には実ファイルとしてコミットされたソースが要る。vendored ファイルをリポジトリに実体として含める(仕組みは Phase 5 で整理) |
| ファイルヘッダ/構造 | 簡易ヘッダ、`tun.c` を参考にした自己流コメント | ASF 標準テンプレートに合わせて全面的に整形。`checkpatch.sh` を通す |
| 実機検証ログ | sim / QEMU のみ | ESP32-S3 実機ログが必須([hardware-verification.md](hardware-verification.md)) |
| LICENSE/NOTICE | 未着手 | `apache/nuttx-apps/LICENSE` に wireguard-lwip の著作権表示を追記 |
| コミット規約 | 未着手(このリポジトリはまだ GSoC 開発用) | `Assisted-by:` タグ運用をこのリポジトリの段階から習慣化しておく |
| PR 粒度 | 全部入りで開発中 | フェーズ単位・機能単位に分割(§3) |
| レビュー体制 | メンター1名想定 | dev@ での早期共有 + 独立レビュアーの確保 |

---

## 3. 段階的 PR 戦略

1本の巨大 PR ではなく、ロードマップの各フェーズに沿って分割する。各 PR は「ビルドが通り、既存機能を壊さない」単位で完結させる(CONTRIBUTING 1.7.5: 個々のコミットが全体のビルド/実行を壊してはならない)。

### PR #1: ビルドシステム統合(Phase 1 相当)
- `apps/netutils/wireguard/` の Kconfig・Makefile・CMakeLists.txt
- vendored `wireguard.c` / `crypto/` (BSD ヘッダそのまま)
- `nuttx-platform.c` / `nuttx-wireguardif.c` はこの時点ではスタブ(現状の Phase 1 相当に戻した最小構成)
- `apache/nuttx-apps/LICENSE` への追記
- `[EXPERIMENTAL]` タグを付けることを検討(独自の UDP ソケット経由 netdev というアーキテクチャはコミュニティにとって前例がなく、設計自体への異論が出る可能性があるため、まず小さく提示して合意を得る)

### PR #2: プラットフォーム層 + netif 統合(Phase 2 相当)
- `nuttx-platform.c` の実装
- `nuttx-wireguardif.c` の netdev 登録・暗号化送受信・タイマー
- sim (`sim:net`) 上での `wg0` 表示ログを添付

### PR #3: NSH コマンド(Phase 4 前倒し分)
- `wg_main.c`(`wg` / `wg show`)

### PR #4: 実機対応 + ドキュメント(Phase 4)
- ESP32-S3 実機ログ
- `Documentation/` 更新(nuttx 本体側に別 PR が必要か要確認 — CONTRIBUTING 1.8 参照)

**PR を出す前に**、まず dev@nuttx.apache.org に設計概要(なぜ lwIP netif ではなく NuttX 独自 netdev として実装したか、UDP ソケットを「配線」に使う設計判断など)を投げて、アーキテクチャ自体への合意を得ることを推奨。これは NuttX 側にとって初めてのパターンなので、コードが仕上がってから晒すより早期に議論した方がやり直しのコストが低い。

---

## 4. 次にやること(優先順)

1. **Assisted-by 運用の開始**: このリポジトリで今後コミットする際、コミットメッセージに `Assisted-by: Claude:claude-sonnet-5` を付ける習慣を今のうちに作る(upstream に出す段階で慌てて過去分を書き直さずに済む)
2. **ESP32-S3 実機ログの取得**: [hardware-verification.md](hardware-verification.md) の手順を実施(§1.2 の必須要件)
3. **`checkpatch.sh`/`nxstyle` によるスタイル整形**: 4つの新規ファイルをテンプレートに合わせて書き直す
4. **`apache/nuttx-apps/LICENSE` 追記案の作成**: wireguard-lwip の著作権表示を Appendix パターンで追記する PR 断片を用意
5. **dev@ メーリングリストでの早期共有**: PR #1 を出す前にアーキテクチャの概要を投げて反応を見る
