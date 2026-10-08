# マージ戦略(2026-10-07 時点の最新 upstream に対して)

[in-kernel-plan.md §4](in-kernel-plan.md#4-マージ戦略) の順序案を、2026-10-07 の upstream と
現行の提出候補(3 コミット)に当てはめ直したもの。**提出の進め方はこの文書を正とする。**
§4 の方針(PR は機能単位、PR 内はコミットで割る、K0 で信用を作る)は変えていない。
変わったのは、PR の切り方を具体化したことと、提出前に塞ぐべき穴が見つかったこと(§3)。

## 1. 最新 upstream への追従結果

| | upstream 先端 | 前回の base(9/28) | 間の upstream コミット | 再適用 |
|---|---|---|---|---|
| apache/nuttx | `20f3b65937`(10/07) | `68dd87f4df` | 108 | **競合なし** |
| apache/nuttx-apps | `ebf379ffe`(10/04) | `b66303e26a` | 5 | **競合なし** |

- 両シリーズを `git replay` で最新 upstream に載せ替えた。`git range-diff` で 3 コミットとも
  **パッチ内容は同一**(`=`)。
  - nuttx: `0eb3055938`(crypto)/ `33856be818`(driver)、ローカルブランチ `rebase/wireguard-driver-20261007`
  - apps: `f8f1d453e`、ローカルブランチ `rebase/wireguard-wg-20261007`
  - **fork への push はまだしていない**。公開済みの `wireguard-*` ブランチは書き換えていない
- 両側が触ったファイルは `net/netdev/netdev_ioctl.c` だけ(upstream `6e14c8cfe5` が
  `SIOCETHTOOL` の処理を追加)。テキストの競合はなく、関数も別。
- ioctl 番号:upstream 側で `SIOCGCANERRORS` が `_SIOC(0x0045)` を使っている。本シリーズは
  `0x0046`〜`0x004A` なので衝突しない(提出直前に再確認する)。
- crypto:base 以降に upstream で `crypto/` を触ったコミットはない(`#20199` は base より前)。
- **ビルド・試験は未実施**。このマシンでは Docker が動いておらず、再適用後の候補は
  まだ一度もビルドしていない。§4 の G5 で行う。
- upstream に WireGuard の PR・issue は他にない(nuttx / nuttx-apps とも検索で 0 件)。
  #9(cxd56 `RTC_HIRES`)と #10(GS2200M usrsock)に相当する修正も、まだ upstream に入っていない。

## 2. PR の切り方

upstream の CONTRIBUTING(10/07 版)で、切り方を決める規則は次の 4 つ。

- 1.7.3 / 1.7.4:PR は小さく 1 機能。別機能は別 PR
- 1.7.5:PR 内のコミットは 1 つずつがビルド・実行・互換を壊さないこと
- 1.7.9:機能を保つために束ねるしかない場合は例外。そのことを PR に明記する
- 1.8:文書は同じ PR に入れ、コードとは別コミットが推奨。**nuttx-apps のコードの文書は nuttx 側に別 PR が要る**

これに従い、現在の「2 + 1 コミット」を次の **6 PR** に組み直す。

```
PR-K0   nuttx       #9  cxd56 RTC_HIRES 起動回帰の修正          独立・小(任意。信用を作る)
PR-K0'  nuttx       #10 usrsock: 管理外 ifname の ioctl         独立・小(任意)
PR-C    nuttx       crypto: ChaCha20-Poly1305 nonce 配置の修正   独立。最初に出す
  │
dev@ 投稿(#3)      設計の告知。返信を 1 週間待つ
  │
PR-K1   nuttx       drivers/net: WireGuard 仮想ネットワークデバイス   PR-C に依存
PR-N    nuttx       tools/nxstyle: system/wg の vendored X25519 を除外   PR-A の前
  │
PR-A    nuttx-apps  system/wg: 設定コマンド                     PR-K1・PR-N のマージ後
PR-D    nuttx       Documentation: system/wg                   PR-A と対で出す
```

### PR-C — `crypto: fix ChaCha20-Poly1305 nonce layout`(nuttx)

現行の crypto コミット 1 本(3 ファイル、+327)を 2 コミットに分ける。

1. `crypto/chachapoly: place the 64-bit nonce in the last eight bytes` — 修正本体(`chachapoly.c`)
2. `crypto/testmngr: add ChaCha20-Poly1305 and XChaCha20-Poly1305 known-answer tests` — KAT

- 既存の in-tree 利用者に出力が変わるものがあるかを PR に書く(RFC 8439 に合わせる修正であること)
- 修正前後のログ(KAT が修正前は落ち、修正後は通る)と、**実機 1 台**での `crypto test OK`
- 説明文は [chachapoly-nonce-draft.md](chachapoly-nonce-draft.md) を流用する

### PR-K1 — `drivers/net: add a WireGuard virtual network device`(nuttx)

現行の driver コミット 1 本(22 ファイル、+5012)を、それぞれ単独でビルドが通る 6 コミットに分ける。

| # | コミット | 中身 | 分ける理由 |
|---|---|---|---|
| 1 | `drivers/net/netdev_upperhalf: fix nxstyle issues` | switch の字下げと空行だけ。動作の変更なし | 整形と機能を混ぜない。レビューで差分を読みやすくする |
| 2 | `drivers/net/netdev_upperhalf: accept IP-only lower halves for WireGuard` | `NET_LL_TUN` を `ip_input` に渡す、非 Ethernet では VLAN 走査をしない | 共有コードの動作変更なので単独で見せる |
| 3 | `net/netdev: add the WireGuard control ioctls` | `include/nuttx/net/ioctl.h` の番号、`include/nuttx/net/wireguard.h`(ABI)、`netdev_ioctl.c` の転送、`netdev_register.c` の pktsize | ABI を独立して議論できるようにする |
| 4 | `drivers/net/wireguard: add the WireGuard device` | `drivers/net/wireguard/`、`net/Kconfig`、`mm/iob/Kconfig`、`drivers_initialize.c`、`drivers/net/Make.defs`、**`LICENSE` 追記** | 本体 |
| 5 | `boards/sim: add the sim:wireguard configuration` | defconfig | これが無いと CI は driver を 1 行もコンパイルしない |
| 6 | `Documentation: add the WireGuard network device` | `wireguard.rst`、索引 | 1.8 の推奨(文書は別コミット) |

- PR 本文に 1.7.9 の例外を明記する:プロトコル核(`wg_noise`)だけでは使い道も試験の方法もないので、デバイスとまとめて 1 PR にする
- コミット 2 は `CONFIG_NET_WIREGUARD` で囲んである。一般の `NET_LL_TUN` 向けにすべきかはレビュアーに委ねる(dev@ で聞く論点に入れる)
- それでも小さくしてほしいと言われたときの分割候補:(a) コミット 1〜3 を前置き PR に出す、(b) 複数ピア・cookie(負荷時の応答)を後続 PR に回す。(b) は削った版の再検証と実機ログの取り直しが必要になるので、**こちらからは提案しない**

### PR-N — `tools/nxstyle: skip the vendored X25519 in system/wg`(nuttx)

現行は driver コミットに入っている `tools/nxstyle.c` の 9 行(`g_white_files` への追加)を切り出す。

- apps 側の CI は nuttx の `nxstyle` で検査されるので、**PR-A より先にマージされている必要がある**。driver とは機能が別なので、K1 に混ぜない
- 代わりに `wg_x25519.c` を NuttX の書式に直して除外そのものをやめる方法もある。ただし元の STROBE と diff を取れなくなるので、除外を選ぶ(現行の判断のまま)

### PR-A — `system/wg: add a WireGuard configuration command`(nuttx-apps)

現行の 1 コミット(7 ファイル、+2245)に、**`LICENSE` 追記**(STROBE の X25519、MIT)を別コミットで足す。

- nuttx-apps の CI は nuttx master に対してビルドするので、**PR-K1 と PR-N のマージ後に出す**
- PR-D(`Documentation/applications/system/wg/index.rst`、現行は driver コミットに入っている)を同時に nuttx 側へ出し、互いにリンクする

## 3. 提出前に塞ぐ穴(見つかったもの)

| # | 穴 | 根拠 | 対応 |
|---|---|---|---|
| H1 | **3 コミットとも `Assisted-by:` が無い** | CONTRIBUTING 1.5:生成 AI を使ったコミットには必須 | 実際に使ったツールとモデルを本人が確定して全コミットに付ける。これまでの記録には Claude Code と Codex の両方が出てくるので、推測では書かない |
| H2 | **`LICENSE` に wireguard-lwip 由来の表示が無い** | `drivers/net/wireguard/wg_noise.c` は BSD-3-Clause(Daniel Hope)。in-kernel-plan §4.1 の (5) で予定していたが、3 コミットに統合したときに入っていない | nuttx の `LICENSE` に追記(PR-K1 コミット 4)。[license-appendix-draft.md](license-appendix-draft.md) は apps 版のファイル一覧なので、カーネル版のファイルで作り直す |
| H3 | **nuttx-apps の `LICENSE` に STROBE X25519(MIT)が無い** | `system/wg/wg_x25519.c` | PR-A に別コミットで追記 |
| H4 | `wg` コマンドの文書が driver コミットに入っている | CONTRIBUTING 1.8:apps のコードの文書は nuttx 側の別 PR | PR-D に移す |
| H5 | `tools/nxstyle.c` が driver コミットに入っている | 1.7.4:別機能は別 PR | PR-N に移す |
| H6 | 再適用後の候補で、実機の試験を一度もしていない | CONTRIBUTING 1.7.2 / 1.9:**実機のビルド・実行ログは必須**。QEMU は数えない | §4 の G5 |
| H7 | `scripts/kernel/verify-pr-series.sh` が「nuttx 2 + apps 1 コミット」を前提にしている | 組み直すと必ず FAIL になる | PR ごとのコミット数とブランチを引数で受け取る形に直す |

## 4. 提出前ゲート(PR ごと)

- **G1** dev@ の設計スレッド(#3)を投稿済みで、1 週間の返信期間が過ぎている(PR-K1 以降)。投稿文は [dev-list-proposal.md](dev-list-proposal.md)。聞く論点に「コミット 2 を一般の `NET_LL_TUN` 向けにすべきか」「ioctl 番号の割り当て」「#14 の時刻の扱い」を入れる
- **G2** H1〜H5 を反映し、各コミットに本人の `Signed-off-by:`。**AI は `Signed-off-by` を付けない**(CONTRIBUTING 1.5)ので、組み直したコミットへの署名は本人が行う
- **G3** コミットごとに `checkpatch.sh -c -u -m -g` が通り、かつ単独でビルドが通る
- **G4** sim 一式(`verify-sim-wg-suite.sh`)、BUILD_KERNEL(1 CPU / SMP 4)、BUILD_PROTECTED、CMake を、**提出するその SHA で**再実行する
- **G5** 実機ログ:ESP32-S3(native Wi-Fi)と Spresense(GS2200M / usrsock)で、提出する SHA の `wg show`・`ping`・往復の抜粋。PR-C は実機 1 台の KAT。**`.config` と鍵は貼らない**
- **G6** PR テンプレート(CONTRIBUTING 2.3)の全項目を埋める。下書きは [pr-drafts.md](pr-drafts.md)(コミット構成の節を本書に合わせて直す)

## 5. 運用の決め事

- **rebase は PR を出す直前に 1 回だけ**。upstream は 10 日で 100 コミット進むが、重なるファイルは
  `netdev_ioctl.c` だけだった。出した後は、競合したときとレビューで修正を求められたときにだけ push する
  (push のたびに CI 一式が走る。CONTRIBUTING 1.7)
- 公開済みの fork ブランチ(`wireguard-crypto` / `wireguard-driver` / `wireguard-wg`)は 9/29 の検証記録と
  対応しているので**上書きしない**。組み直したものは PR ごとの新しいブランチ名で push する
- PR は依存の順に、前の PR がマージされてから次を出す(依存は `Depends on #NNNN` で明記する)。並行して出してよいのは
  PR-K0 / K0' / C と、対になる PR-A / PR-D だけ

## 6. 本人が決めること

1. **`Assisted-by:` に書くツール名とモデル名**(H1)
2. **出す時期**:CoC Glasgow(10/11〜14)の前に PR-C と dev@ だけ出すか、すべて発表の後にするか。
   スライド 37 は「upstream の議論も PR もまだ」と書いているので、前に出すならスライドも直す
3. PR-K0 / K0'(#9 / #10)を先に出すか
4. `[EXPERIMENTAL]` を付けるか。#14(RTC が無い機器での再起動後の再接続)は判断として未解決のまま。
   本書の案は**付けない**。代わりに Kconfig のヘルプと文書に制約を明記する(現行どおり)
