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

これが今のプロジェクト進捗における一番大きいギャップ。現状の検証は sim / QEMU のみで、**実機(ESP32-S3)でのビルド・実行ログがない状態では通常の PR ですら受理されない**。[../development/hardware-verification.md](../development/hardware-verification.md) の手順を実施し、ログを取得することが upstream 提出の前提条件になる。

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

最終更新: 2026-08-22。

| 項目 | 現状 | 必要な対応 |
|---|---|---|
| ビルド方式 | Docker が `wireguard-lwip` を都度 `git clone` してコピーし、`nuttx_port/` を上書き | ⛔ upstream PR には実ファイルとしてコミットされたソースが要る。vendored ファイルをリポジトリに実体として含める(仕組みは Phase 5 で整理) |
| ファイルヘッダ/構造 | ✅ **完了**。4ファイルとも ASF ヘッダ + 標準セクション構成に整形し、`checkpatch.sh` をクリーンに通過(2026-08-22) | — |
| 実機検証ログ | ✅ **完了**。ESP32-S3 実機で handshake / ping / TCP(telnet・HTTP)疎通を確認 | — |
| LICENSE/NOTICE | 🔶 追記案を [license-appendix-draft.md](license-appendix-draft.md) に用意。実際の `LICENSE` への反映は未 | 提出時に最新の書式に合わせて反映。`wireguardif.c` / `crypto/cortex/` を vendored ツリーから外すか判断する |
| コミット規約 | 🔶 `Assisted-by:` タグの運用を 2026-08-22 のコミットから開始 | 過去分は書き直さず、以後徹底 |
| PR 粒度 | 全部入りで開発中 | フェーズ単位・機能単位に分割(§3) |
| レビュー体制 | メンター1名想定 | dev@ での早期共有 + 独立レビュアーの確保 |
| 実行時設定 | ⛔ 鍵・ピアが Kconfig 固定。鍵がビルド成果物に焼き込まれる | `wg setconf` 相当の実装([../development/code-review-2026-08.md](../development/code-review-2026-08.md) §1-D) |

---

## 3. 段階的 PR 戦略

1本の巨大 PR ではなく、ロードマップの各フェーズに沿って分割する。各 PR は「ビルドが通り、既存機能を壊さない」単位で完結させる(CONTRIBUTING 1.7.5: 個々のコミットが全体のビルド/実行を壊してはならない)。

### PR #1: ビルドシステム統合 + vendored ソース

- `apps/netutils/wireguard/` の `Kconfig` / `Makefile` / `CMakeLists.txt` / `Make.defs`
- vendored 一式(`wireguard.c` / `crypto.c` / `crypto/refc/*`)を元の BSD ヘッダのまま
- `wireguard-platform.h`(ピア数・allowed-ips 上限を Kconfig 化した NuttX 版)
- `nuttx-platform.c`(OS 依存4関数)
- `apache/nuttx-apps/LICENSE` への追記(下書きは [license-appendix-draft.md](license-appendix-draft.md))

この時点では `wg0` を登録しないので機能としては何もしないが、**ビルドが通り既存を壊さない**単位として成立する。

> **注意:** `wireguardif.c` / `crypto/cortex/` は**含めない**。ビルド対象外であり、
> 持ち込むと LICENSE の記載対象が無駄に広がる(対応済み — `Dockerfile` の base ステージで削除している)。

### PR #2: netdev 統合(このシリーズの本体)

- `nuttx-wireguardif.c` / `nuttx-wireguardif.h`
- `wg_main.c` の最小形(`wg up` / `wg show`)

**レビューの焦点がここに集中する**ので、PR 説明で以下を明示する:

- `NET_LL_TUN` netdev + UDP ソケットという構成の理由(`drivers/net/` 案・TUN + デーモン案との比較)
- **`psock_*()` 依存による FLAT ビルド前提**([Issue #6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6))— fd がタスクグループ単位でスコープされるため fd ベースでは `EBADF` になる経緯も添える
- 実機ログ(ESP32-S3 / Spresense)と sim・QEMU の検証スクリプト出力

### PR #3: 実行時設定

- `wg genkey` / `pubkey` / `set` / `setconf` / `showconf` / `down`
- `CONFIG_NET_WIREGUARD_CONFIG_PATH`
- 設定形式が `wg(8)` 互換であること(デスクトップの設定ファイルと相互運用できる)を PR 説明に書く

PR #2 と分けるのは、netdev の設計に異論が出た場合にこちらが巻き添えにならないようにするため。

### PR #4: 複数ピア

- `CONFIG_NET_WIREGUARD_MAX_PEERS` / `MAX_SRC_IPS`
- 実測した RAM コスト(1ピアあたり約 904 B)を PR 説明に含める

### PR #5: ドキュメント

- `Documentation/` の更新(nuttx 本体側に別 PR が必要か要確認 — CONTRIBUTING 1.8 参照)

---

### 各 PR に添付する検証ログ

CONTRIBUTING は実機ログを必須としている。`scripts/` の4本はいずれも**本物の Linux カーネル WireGuard** を相手にしており、出力をそのまま添付できる:

| スクリプト | 添付先 |
|---|---|
| `verify-sim-wireguard.sh` / `verify-qemu-wireguard.sh` | PR #2 |
| `verify-sim-wg-runtime.sh` | PR #3 |
| `verify-sim-wg-multipeer.sh` | PR #4 |

実機ログ(ESP32-S3 の実 Wi-Fi ハンドシェイク・TCP 疎通、Spresense の `wg0` 起動)は PR #2 に添える。

---

**PR を出す前に**、まず dev@nuttx.apache.org に設計概要(なぜ lwIP netif ではなく NuttX 独自 netdev として実装したか、UDP ソケットを「配線」に使う設計判断など)を投げて、アーキテクチャ自体への合意を得ることを推奨。これは NuttX 側にとって初めてのパターンなので、コードが仕上がってから晒すより早期に議論した方がやり直しのコストが低い。

---

## 4. 次にやること(優先順)

1. ~~**Assisted-by 運用の開始**~~ ✅ 2026-08-22 のコミットから適用開始
2. ~~**ESP32-S3 実機ログの取得**~~ ✅ 完了([../development/phase4-log.md](../development/phase4-log.md))
3. ~~**`checkpatch.sh`/`nxstyle` によるスタイル整形**~~ ✅ 完了(4ファイルともクリーン、以後も維持)
4. ~~**`apache/nuttx-apps/LICENSE` 追記案の作成**~~ ✅ 下書き完了([license-appendix-draft.md](license-appendix-draft.md))。記載内容は実ファイルと照合済み
5. ~~**vendored ツリーの整理**~~ ✅ `wireguardif.c` / `crypto/cortex/` を除外(`Dockerfile` の base ステージ)。**残るのは Docker の `git clone` 方式から実ファイル同梱方式への移行** — upstream には実ファイルとしてコミットする必要があるため、提出時に一度だけ必要な作業
6. **dev@ メーリングリストでの早期共有** ← **次にやるべきはこれ**。投稿ドラフトは [dev-list-proposal.md](dev-list-proposal.md)、追跡は [Issue #3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3)。設計をひっくり返しうる2点(netdev 配置・FLAT ビルド前提)を先に問う構成にしてある
7. **FLAT ビルド前提の扱いを決める**([Issue #6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6))。6 の反応次第で、Kconfig で `depends on` を付けて明示するか、別設計に変えるかが決まる

---

## 5. 提出前チェックリスト

コードは揃っているので、残るのは合意形成と提出作業。

- [ ] dev@ に設計を投稿し、netdev 配置への合意を得る(項目 6)
- [ ] FLAT ビルド制約の扱いを決める(項目 7)
- [ ] vendored ソースを実ファイルとしてコミットする形に移行(項目 5 の後半)
- [ ] 最新の `apache/nuttx-apps/LICENSE` の書式に合わせて追記案を確定
- [ ] メンターにライセンス記載と PR 分割をレビューしてもらう
- [ ] 各 PR に対応する検証ログを添付(§3 の表を参照)
- [ ] 独立した2名以上のレビュアーを確保(§1.5 — メンターだけでは足りない可能性がある)
