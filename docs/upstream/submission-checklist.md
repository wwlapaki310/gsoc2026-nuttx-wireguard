# 提出チェックリスト(2026-10-10、2026-10-12 追記)

> **2026-10-12:** 公式ルール(CONTRIBUTING.md、Documentation/contributing/、CI の workflow)を
> 最新の upstream で読み直し、照合した結果を **§6 「公式ルールとの照合 (2026-10-12)」** にまとめた。
> §3 (a) の署名コマンドと §3 (e) の PR 本文は §6 が新しい(依存の書き方、件名の形、
> `Assisted-by:` の文面、sim:wireguard の CI 失敗)。§1〜§5 は 10/10 時点の記録として残す。

[merge-strategy.md](merge-strategy.md) §2 の分割と §3 の H2〜H5・H7 を実施した結果と、
**本人の手作業として残っているものだけ**をまとめた。手順はそのまま貼って実行できる形にしてある。

- ブランチはすべて**ローカルのみ**。fork への push、PR、dev@ 投稿はまだ何もしていない
- 公開済みの `origin/wireguard-crypto` / `wireguard-driver` / `wireguard-wg` と、候補ブランチ
  `rebase/wireguard-driver-20261007` / `rebase/wireguard-wg-20261007` は**触っていない**
- 新しいコミットには **`Signed-off-by:` も `Assisted-by:` も付いていない**(CONTRIBUTING 1.5:
  AI は署名しない。`Assisted-by:` の文面は本人が決める)。§3 (a) の 1 コマンドで両方付く
- 作者(Author)は候補と同じ `satoru akita <wwlap24@gmail.com>`。コミッターはこの PC の git 設定

## 1. ブランチと PR の対応

2 系統ある。**中身は同じ**で、載っている upstream だけが違う(`git range-diff` で全コミット `=`。
違うのは文書 2 コミットのパスだけ。§1.3)。

### 1.1 `pr/*` — 候補と同じ base(nuttx `20f3b65937` / apps `ebf379ffe2`)

検証(§2)はすべてこの系統の SHA で行った。

| PR | リポジトリ | ブランチ | コミット | 依存 |
|---|---|---|---|---|
| PR-C | nuttx | `pr/crypto-chachapoly` | `21aa61fd41` crypto/chachapoly: place the 64-bit nonce in the last eight bytes<br>`2f8d4526d9` crypto/testmngr: add ChaCha20-Poly1305 and XChaCha20-Poly1305 KATs | なし |
| PR-N | nuttx | `pr/nxstyle-wg` | `4bd8b4a296` tools/nxstyle: skip the vendored X25519 in system/wg | なし |
| PR-K1 | nuttx | `pr/wireguard-driver`(`pr/crypto-chachapoly` の上) | `9eb9bd3210` net: fix nxstyle issues in netdev_upperhalf.c and netdev_register.c<br>`55ea972ee7` drivers/net/netdev_upperhalf: accept IP-only lower halves for WireGuard<br>`1b5b8b8019` net/netdev: add the WireGuard control ioctls<br>`556cb3aa7b` drivers/net/wireguard: add the WireGuard device(+ `LICENSE`)<br>`6dcf148a8c` boards/sim: add the sim:wireguard configuration<br>`5ba13e4296` Documentation: add the WireGuard network device | PR-C |
| PR-D | nuttx | `pr/doc-system-wg`(`pr/wireguard-driver` の上) | `af389024d2` Documentation: add the system/wg command | PR-K1 |
| PR-A | nuttx-apps | `pr/system-wg` | `5a443e4d3` system/wg: add a WireGuard configuration command<br>`efa4c0bf0` LICENSE: add the STROBE X25519 used by system/wg | PR-K1・PR-N |

### 1.2 `pr-latest/*` — 2026-10-10 の upstream 先端(nuttx `0916f1a561` / apps `da92a492b`)

> **2026-10-12 更新:** `pr-latest/*` は 10/11 の upstream 先端(nuttx `4bc1ab2fd7` / apps
> `74df79a53`)に載せ替え、件名を公式の形に直し、sim:wireguard の defconfig を正規形にした。
> **下の表の SHA は旧版**(`bak/pr-latest-20261010/*` として残してある)。現行の SHA は §6.1。

10/07 以降 upstream は nuttx 201・apps 13 コミット進んでいた。§1.1 を載せ替えたもの(試行)。

| PR | ブランチ | 先端 |
|---|---|---|
| PR-C | `pr-latest/crypto-chachapoly` | `18ece1fced`(`bfaa93b62c` → `18ece1fced`) |
| PR-N | `pr-latest/nxstyle-wg` | `8f294bbbd9` |
| PR-K1 | `pr-latest/wireguard-driver` | `bf6cd96143`(6 コミット `ae1a06fea4` … `bf6cd96143`) |
| PR-D | `pr-latest/doc-system-wg` | `60ad561a9e` |
| PR-A | apps `pr-latest/system-wg` | `304f6255c`(`fc302caad` → `304f6255c`) |

### 1.3 載せ替えで分かったこと

- **upstream `f6ecf80ebb`(10/08)で文書の構成が変わった。** `Documentation/components/` が
  `Documentation/os/` に移動した。`pr-latest/*` では次の 3 か所を追従させてある(コード側は競合なし):
  - `Documentation/os/drivers/special/net/wireguard.rst`(旧 `components/drivers/special/net/`)
  - 同じディレクトリの `index.rst` への 1 行
  - `applications/system/wg/index.rst` からのリンク `:doc:` を `/os/drivers/special/net/wireguard` に
- `tools/nxstyle.c` は upstream `7b58ee84fe` が `g_white_prefix[]` に追記していたが、本シリーズは
  `g_white_files[]` なので競合しない
- **どちらの系統で出すか**は本人が決める。提出直前にもう一度 upstream が動くので、
  §3 (a) の前に `git rebase` し直すのが前提(§5 の「rebase は出す直前に 1 回」)。`pr-latest/*` を
  起点にすれば、その rebase は文書パスの対応が済んだ状態から始められる

### 1.4 候補との差分(コードは何も落ちていない)

`git diff` で確認した。分割後の木と候補の木の差は次だけ。

| 差分 | 理由 |
|---|---|
| nuttx `LICENSE` +35 行 | H2。`drivers/net/wireguard/wg_noise.[ch]`(BSD-3-Clause, Daniel Hope)を既存エントリの書式で末尾に追記。ファイル見出しが BSD なのはこの 2 つだけ(`wireguard.c` などは Apache-2.0 見出しで、wireguard-lwip の記述もない) |
| apps `LICENSE` +28 行 | H3。`system/wg/wg_x25519.[ch]` の STROBE MIT を既存エントリの書式で追記 |
| `Documentation/applications/system/wg/index.rst` 見出し線 | 候補は表題 35 文字に対して上下線が 33 文字。docutils が `Title overline too short` を出す = NuttX の文書 CI(`sphinx -W`)で落ちる。35 文字に直した |
| `wireguard.rst` の 1 行(PR-K1 のみ) | `:doc:` で wg コマンドの文書を指していた。PR-D が無い状態では `-W` で落ちるので、PR-K1 では文字だけにし、PR-D でリンクに戻す。PR-D まで重ねた木は候補と同じ |
| `system/wg/wg_x25519.c` +12 行(PR-A) | 候補は CI と同じ `EXTRAFLAGS="-Wno-cpp -Werror"` でビルドが落ちる(`-Wundef` の `X25519_SUPPORT_VERIFY`、GCC の `-Wstringop-overread` 誤検知)。適応ブロックに `#define X25519_SUPPORT_VERIFY 0`(従来と同じ動作)と GCC 11 以降限定の pragma を足した。STROBE 由来の本体は 1 文字も変えていない |

`tools/nxstyle.c` は PR-N にだけあり、PR-K1 側には無い(候補の driver コミットからの移動)。

## 2. 検査結果

すべて Docker(`ubuntu:24.04`、gcc 13.3、kconfig-frontends、ローカル clone を mount して中で
`git clone --shared`)で実行。ネットワークは proxy なし。

### 2.1 書式(checkpatch / nxstyle)

`scripts/kernel/verify-pr-series.sh`(H7 で書き直し。§4)で、**各コミットを 1 つずつ checkout して**
`tools/checkpatch.sh -c -u -g HEAD~1..HEAD` と、メッセージ検査 `-m`(署名だけ仮の行を足して検査)を実行。

| 系統 | 結果 |
|---|---|
| `pr/*` 全 12 コミット(nuttx 10・apps 2) | **全コミット `All checks pass`**(パッチ・メッセージとも) |
| `pr-latest/*` 全 12 コミット | 同上 |
| `--require-signoff` | 期待どおり全コミット `Missing Signed-off-by` で FAIL(署名後に再実行する。§3 (a)) |
| apps を base の nxstyle で検査 | 期待どおり `wg_x25519.c` で FAIL。PR-A は PR-N のマージが前提であることの確認 |

nxstyle を直接、触ったすべての `.c` / `.h`(nuttx 16 ファイル、apps 3 ファイル)に各ブランチ先端で
実行して全件エラーなし。修正が必要な書式の問題は見つからなかった。

### 2.2 コミットごとのビルド(CONTRIBUTING 1.7.5)

sim、CI と同じ `EXTRAFLAGS="-Wno-cpp -Werror"`。nuttx 側は apps を upstream base(`ebf379ffe2`、
system/wg なし)にして、PR-K1 の CI と同じ条件にした。

| コミット | 構成 | 結果 |
|---|---|---|
| `21aa61fd41` | sim:nsh + CRYPTO + ALGTEST | PASS(警告 0) |
| `2f8d4526d9` | 同上 | PASS、起動して `crypto test OK`(driver なし。`verify-crypto-prerequisite.py`) |
| `9eb9bd3210` `55ea972ee7` `1b5b8b8019` | sim:wireguard の defconfig(この時点では `NET_WIREGUARD` が無いので落ちる) | PASS(警告 0) |
| `556cb3aa7b` `6dcf148a8c` `5ba13e4296` | sim:wireguard | PASS(警告 0)、`wg0` が登録される(`verify-driver-prerequisite.py`) |
| apps `5a443e4d3` `efa4c0bf0` | sim:wireguard(nuttx `5ba13e4296`) | PASS(警告 0) |
| 参考: apps 候補 `f8f1d453e` | 同上 | **FAIL**(§1.4 の 2 警告。修正の根拠) |

PR-C の修正前後(PR 本文用): `2f8d4526d9` の KAT を修正前の `chachapoly.c` で動かすと
`test_chacha20poly1305: ERROR: Failed chacha20poly1305 encrypt #1` / `crypto test failed`、
修正後は `crypto test OK`。counter 0 は一致し 1 から落ちる、という説明どおり。

文書: `pr-latest/doc-system-wg`(`60ad561a9e`)と `pr-latest/wireguard-driver`(`bf6cd96143`)で
upstream の文書 CI と同じ `pipenv sync && make html`(`-W`、python 3.10)が PASS。

### 2.3 先端での一式(G4)

nuttx `5ba13e4296`(PR-K1 先端。PR-D 先端 `af389024d2` とコードは同じ)+ apps `efa4c0bf0`。

| 項目 | 結果 |
|---|---|
| sim:wireguard ビルド(`./tools/configure.sh sim:wireguard`) | PASS(警告 0) |
| sim 一式 `verify-sim-wg-suite.sh`(T1 runtime, ioctl 負例, KAT, negotiation 負例, multipeer, replay/cookie/fuzz, zeroize+自然 rekey, keyfile faults, publisher 11 ケース) | **全 PASS**(369 秒)。相手は Linux カーネルの WireGuard |
| CMake/Ninja(`build-cmake-wg.sh sim:nsh`) | PASS。`wireguard.c.o` `wg_crypto.c.o` `wg_noise.c.o` `wg_tai64n.c.o` `wg_main.c.o` を確認 |
| BUILD_KERNEL `rv-virt:knetnsh64`(1 CPU) | ビルド PASS、`verify-knetnsh-wg.sh` PASS(両方向のトンネル、flood と再設定と down/up の後に復旧) |
| BUILD_KERNEL `rv-virt:knetnsh64_smp`(4 CPU) | 同上 PASS |
| BUILD_PROTECTED `rv-virt:pnsh64`(`build-pnsh-wg.sh` + `verify-pnsh-wg.sh`) | ビルド PASS。実行は**間欠的に FAIL**(下記) |

`pr-latest/*` でも、全 12 コミットの `-Werror` コミット別ビルド、sim:wireguard ビルド、sim 一式、
CMake がすべて PASS(nuttx `bf6cd96143` + apps `304f6255c`)。BUILD_KERNEL/PROTECTED は
`pr-latest/*` では回していない。

**BUILD_PROTECTED の間欠障害(新しく見つかったもの。分割とは無関係):**
同じイメージで `verify-pnsh-wg.sh` を繰り返すと、`pr/*` で 8 回中 2 回、**分割前の候補
(`33856be818` + `f8f1d453e`)でも 9 回中 1 回**、`wg` の設定中に落ちる
(`FAIL: the guest faulted while configuring wg0 across the MPU boundary`)。最初の例外は
ユーザー空間の `nxsem_wait`(`libs/libc/semaphore/sem_wait.c:141`、task `wg`)で、読もうとした番地が
でたらめ(`MTVAL` 0x1a3efe など)。2 つ目はその後のダンプ中のカーネル側(`nxtask_argvstr`)で二次的。
9/29 の記録は 1 回の PASS だったので見えていなかった。原因は未調査(`wg` プロセスのスタック不足や
ユーザー側のポインタ破壊が候補だが、推測)。**PR-K1/PR-A の本文で PROTECTED 実行を「PASS」と
書かないこと。** 書くなら「ビルドは通る。QEMU での実行は間欠的に user 側で fault する(調査中)」。

### 2.4 やっていないこと

- **実機(G5)**: 一度も動かしていない。§3 (b)
- BUILD_PROTECTED の間欠 fault の原因調査(§2.3)
- macOS(Darwin)の sim ビルドは CI にあるが試せていない。clang の `-Werror` で新しい警告が出る可能性は残る
- 署名後の SHA での再検査。署名はメッセージしか変えないので木は同じだが、G4 は「提出する SHA で」なので、
  §3 (a) の後に少なくとも `verify-pr-series.sh --require-signoff` と sim:wireguard ビルドは回し直す

## 3. 本人の手作業

### (a) `Assisted-by:` を決めて全コミットに署名する

CONTRIBUTING 1.5 の順序は「`Assisted-by:` が署名の上」。`git commit -s` は署名を先に置くので、
2 つとも `--trailer` で順に付ける。**これまでの記録には Claude Code と Codex の両方が出てくるので、
実際に使ったものを本人が確定して書く**(書式は `AGENT_NAME:MODEL_VERSION [TOOL...]`。1 行に
収まらなければ `Assisted-by:` を 2 行にする)。

```bash
# 2 つのリポジトリで共通に使う
AB='Assisted-by: <AGENT_NAME>:<MODEL_VERSION>'      # ← 本人が書く
SOB='Signed-off-by: satoru akita <wwlap24@gmail.com>' # ← 9/29 の署名と同じ名義。変えるならここ
SIGN="git commit --amend --no-edit --trailer \"$AB\" --trailer \"$SOB\""
```

以下は `pr/*` の場合。`pr-latest/*` で出すなら、ブランチ名と base を読み替える
(nuttx の base は `0916f1a561`、apps は `da92a492b`)。

```bash
cd /c/Users/0000400096/workspaces/nuttx
git status --short            # 空であること
# PR-C / PR-K1 / PR-D は積み重なっているので、一番上で 1 回やれば --update-refs で 3 本とも書き換わる
git switch pr/doc-system-wg
git rebase --update-refs --exec "$SIGN" 20f3b65937
git switch pr/nxstyle-wg
git rebase --exec "$SIGN" 20f3b65937
git switch master

cd /c/Users/0000400096/workspaces/nuttx-apps
git switch pr/system-wg
git rebase --exec "$SIGN" ebf379ffe2
git switch master
```

確認(中身が変わっていないこと、署名と順序):

```bash
cd /c/Users/0000400096/workspaces/nuttx
git range-diff 20f3b65937..5ba13e4296 20f3b65937..pr/wireguard-driver   # 全部 '!' でメッセージだけ差
git diff --stat af389024d2 pr/doc-system-wg                              # 空
git log --format='%h %(trailers:key=Assisted-by,key=Signed-off-by,separator=%x20|%x20)' 20f3b65937..pr/doc-system-wg
```

署名後の形式検査(Docker の中で。`/opt/nuttx` と `/opt/apps` は 2.1 と同じ clone。`--tools` は
`pr/nxstyle-wg` を checkout した別の作業木):

```bash
verify-pr-series.sh --require-signoff --tools /opt/tools \
  /opt/nuttx:20f3b65937..pr/crypto-chachapoly=2 \
  /opt/nuttx:20f3b65937..pr/nxstyle-wg=1 \
  /opt/nuttx:pr/crypto-chachapoly..pr/wireguard-driver=6 \
  /opt/nuttx:pr/wireguard-driver..pr/doc-system-wg=1 \
  /opt/apps:ebf379ffe2..pr/system-wg=2
```

### (b) 実機ログ(G5)

CONTRIBUTING 1.7.2:コード変更の PR には**実機 1 台以上のビルド・実行ログが必須**。QEMU/sim は数えない。
**提出する SHA(署名後の SHA)でビルドしたイメージ**で取る。

| 機器 | PR | 取るもの |
|---|---|---|
| ESP32-S3(native Wi-Fi) | PR-K1, PR-A(PR-C も兼ねてよい) | 下の一式 |
| Spresense(GS2200M / usrsock、FLAT) | PR-K1, PR-A | 下の一式。master では cxd56 の `RTC_HIRES` 起動回帰(#9)があるので `docs/upstream/patches/cxd56-rtc-hires-boot.patch` を当てて作る。**当てたことを PR 本文に書く**(その修正は提出物に含めない) |
| どちらか 1 台 | PR-C | `CONFIG_CRYPTO_ALGTEST=y` で起動ログの `crypto test OK` |

一式(NSH のコピー):

1. ビルドした SHA:`git -C nuttx log -1 --format=%H` と apps 側の同じもの(ログの先頭に書く)
2. 起動ログの先頭と `uname -a`
3. `ifconfig wg0`
4. `wg show`(公開鍵・endpoint・handshake・転送量が出る。秘密鍵は出ない)
5. 機器 → 相手、相手 → 機器の両方向の `ping`(トンネルアドレスで)
6. `wg down` → `wg up` 後にもう一度 `ping`(任意。あると強い)

**絶対に貼らないもの:**

- `.config`(Wi-Fi の SSID/パスフレーズを焼き込んだ構成では、そのまま入っている)
- 秘密鍵・PSK:`wg genkey` の出力、`wg showconf` の出力(**秘密鍵を含む**)、`wg0.conf` の中身、
  `wg set private-key ...` を打った行
- Wi-Fi の認証情報、`wapi` の psk を打った行、相手側 `wg show` の `private key` 行
- 自宅・社内の外部 IP アドレス(endpoint が公開アドレスなら伏せる)

貼る前に確認:

```bash
grep -nE '[A-Za-z0-9+/]{43}=' log.txt   # 出たら公開鍵か確認。秘密鍵・PSK なら消す
grep -niE 'psk|pass|private' log.txt
```

### (c) dev@ への投稿(G1)

1. [dev-list-proposal.md](dev-list-proposal.md) の「送る前に確認すること」を済ませる
   (メンターに先に見てもらう。本文のリンクは fork push 後の URL にする → (d) の後)
2. 聞く論点に次を入れる(merge-strategy §4 G1):コミット `55ea972ee7`(NET_LL_TUN を
   `CONFIG_NET_WIREGUARD` で囲った)を一般の `NET_LL_TUN` 向けにすべきか/ioctl 番号
   `0x0046..0x004a`/#14 の時刻の扱い
3. 件名 `[DISCUSS] In-kernel WireGuard for NuttX: a NET_LL_TUN netdev + ioctl ABI` で
   dev@nuttx.apache.org に送る
4. **1 週間待つ**。返信で設計が変わったら分割を作り直す(このチェックリストの SHA は無効になる)

PR-C と PR-N は dev@ を待たずに出してよい(merge-strategy §5)。

### (d) fork への push

公開済みの `wireguard-*` は上書きしない。PR ごとに新しい名前で push する。

```bash
cd /c/Users/0000400096/workspaces/nuttx
git push origin pr/crypto-chachapoly:wg-pr-crypto-chachapoly
git push origin pr/nxstyle-wg:wg-pr-nxstyle
git push origin pr/wireguard-driver:wg-pr-driver
git push origin pr/doc-system-wg:wg-pr-doc-system-wg

cd /c/Users/0000400096/workspaces/nuttx-apps
git push origin pr/system-wg:wg-pr-system-wg
```

(`pr-latest/*` で出すならローカル側の名前を読み替える。リモート側の名前は同じでよい)

### (e) PR を出す(依存の順)

本文は [pr-drafts.md](pr-drafts.md) と [chachapoly-nonce-draft.md](chachapoly-nonce-draft.md) から作る。
**PR テンプレート(CONTRIBUTING 2.3)の全項目を埋める**こと。下では本文をファイルに切り出して
`--body-file` で渡す。`$TMP` は任意の作業ディレクトリ。

PR-K1 は PR-C の上に積んであるので、PR-C がマージされるまで K1 の差分には crypto の 2 コミットも
見える。**K1 は PR-C のマージ後に、upstream に rebase してから出す**(順序は merge-strategy §5)。

```bash
# 1. PR-C(最初。単独)
gh pr create -R apache/nuttx -B master -H wwlapaki310:wg-pr-crypto-chachapoly \
  -t 'crypto: fix ChaCha20-Poly1305 nonce layout' \
  --body-file "$TMP/pr-c.md"       # chachapoly-nonce-draft.md の Symptom/Cause/Fix + §2.2 の修正前後ログ + 実機の crypto test OK

# 2. PR-N(PR-C と並行可)
gh pr create -R apache/nuttx -B master -H wwlapaki310:wg-pr-nxstyle \
  -t 'tools/nxstyle: skip the vendored X25519 in system/wg' \
  --body-file "$TMP/pr-n.md"       # 下の「PR-N 本文」

# --- dev@ の 1 週間(c)と PR-C のマージを待つ ---
# PR-C マージ後: pr/wireguard-driver を upstream/master に rebase、署名を確認、再 push(-f はこのブランチだけ)

# 3. PR-K1
gh pr create -R apache/nuttx -B master -H wwlapaki310:wg-pr-driver \
  -t 'drivers/net: add a WireGuard virtual network device' \
  --body-file "$TMP/pr-k1.md"      # pr-drafts.md の「PR-K1」節 + 1.7.9 の例外 + "Depends on #<PR-C>" + 実機ログ

# --- PR-K1 と PR-N のマージを待つ ---

# 4. PR-A と PR-D(対で出し、互いにリンクする)
gh pr create -R apache/nuttx-apps -B master -H wwlapaki310:wg-pr-system-wg \
  -t 'system/wg: add a WireGuard configuration command' \
  --body-file "$TMP/pr-a.md"       # pr-drafts.md の「PR-A1」節 + "Depends on apache/nuttx#<K1>, #<N>" + 実機ログ
gh pr create -R apache/nuttx -B master -H wwlapaki310:wg-pr-doc-system-wg \
  -t 'Documentation: add the system/wg command' \
  --body-file "$TMP/pr-d.md"       # 下の「PR-D 本文」+ PR-A へのリンク
```

PR-D も PR-K1 の上に積んである。K1 のマージ後に upstream へ rebase してから出す。

**PR-N 本文(下書き):**

```
## Summary

apps/system/wg (apache/nuttx-apps PR to follow) carries an X25519
implementation taken from the STROBE project under the MIT license. Its
algorithm body keeps the upstream formatting so it can still be diffed
against the STROBE source. This adds wg_x25519.c and wg_x25519.h to
g_white_files[], as is done for other vendored sources, so that the
nuttx-apps style check accepts them.

## Impact

tools/nxstyle only. No effect on any build.

## Testing

nxstyle on apps system/wg/wg_x25519.c fails before this change and passes
after it; tools/nxstyle.c itself passes checkpatch.
```

**PR-D 本文(下書き):**

```
## Summary

Documentation for the wg command added by apache/nuttx-apps#<PR-A>: its
configuration, subcommands, how it keeps the private key in the
configuration file, and an example session. Links it from the WireGuard
device page added by #<K1>.

## Impact

Documentation only.

## Testing

make html (sphinx -W) passes.
```

## 4. `verify-pr-series.sh`(H7)

`scripts/kernel/verify-pr-series.sh` は PR ごとに `<tree>:<base>..<tip>=<コミット数>` を引数で受け取る形にした。
各コミットを checkout して 1 つずつ検査する(旧版は先端の木で範囲全体を 1 回だけ見ていた)。
nuttx の木はそれぞれ自分の `checkpatch.sh` で検査する(nxstyle はビルドした木の TOPDIR を
埋め込むので、別の木の nuttx ファイルを「リポジトリ外」として落とす)。apps は `--tools` の nuttx を使う。
使い方はスクリプト冒頭のコメント。

## 5. 判断が要るもの(merge-strategy §6 に追加)

1. `pr/*` と `pr-latest/*` のどちらから出すか(§1.3)
2. コミット 1 の題を `net: fix nxstyle issues in netdev_upperhalf.c and netdev_register.c` にした
   (merge-strategy では upperhalf だけ。`netdev_register.c` にも整形だけの差分があり、これも
   コミット 3 から外して整形コミットに寄せた)
3. `wg_x25519.c` の警告対策(§1.4)を PR-A に入れてよいか。入れないと sim:wireguard が CI の
   `-Werror` で落ちる(PR-A の CI と、両方マージ後の nuttx の CI の両方)
4. BUILD_PROTECTED の間欠 fault(§2.3)を PR 前に追うか、既知の問題として PR に書いて出すか

## 6. 公式ルールとの照合 (2026-10-12)

2026-10-12 に両 clone で `git fetch upstream` し、**upstream 先端**で一次資料を読み直した。
nuttx `4bc1ab2fd7`(2026-10-11)、nuttx-apps `74df79a53`(2026-10-11)。行番号はこの版のもの。
10/07 に読んだ版から `CONTRIBUTING.md` は変わっていない(`git diff 20f3b65937 upstream/master -- CONTRIBUTING.md`
が空。`Assisted-by:` の規定は 7/11 の `9748c760be` で入ったもの)。公式サイトの
<https://nuttx.apache.org/docs/latest/contributing/guide.html> は同じ `CONTRIBUTING.md` を取り込んだもので
(`Documentation/contributing/guide.rst` が `.. include:: ../../CONTRIBUTING.md`)、文面も同じだった。

### 6.0 読むべきもの一覧(両リポジトリ)

**nuttx-apps には `CONTRIBUTING.md` が無い。** apps の PR テンプレートの 1 行目が nuttx の
`CONTRIBUTING.md` を指していて、apps も同じ規則に従う。どちらのリポジトリにも `AGENTS.md` や AI/LLM 方針の
専用ファイルは**無い**(`git ls-tree -r` で `agents|claude|copilot|llm|cursor` を検索して 0 件)。AI の規定は
`CONTRIBUTING.md` 1.5 / 1.6 だけで、そこから ASF の方針と Linux カーネルの文書を参照している。

| パス(nuttx `4bc1ab2fd7` / apps `74df79a53`) | 何を求めているか |
|---|---|
| nuttx `CONTRIBUTING.md` | 規則の本体(1.1〜1.17)とテンプレート(2.1 ソース / 2.2 コミット / 2.3 PR)。§6.1 |
| nuttx `INVIOLABLES.md`(`Documentation/introduction/inviolables.rst`) | CONTRIBUTING の冒頭(L5-7)が最初に読めと指示。POSIX 準拠、モジュール構造、コーディング規約の厳守(L43-51)、ライセンス(L56-61:BSD-3 互換・MIT など。コピーレフト不可)、全ユーザー・全ツールチェーン(L63-79) |
| nuttx `Documentation/contributing/workflow.rst` | 書式は nxstyle / checkpatch で確認(L15-22)。整形は機能と別コミット(L26-28)。重い変更は先にメーリングリストで(L30-31)。オリジナルは Apache-2.0 の見出しで(L34-36)。**会社員なら著作権は会社にあることが多く、会社の許可と契約が要る(L38-40)**。**活発に開発中の外部プロジェクトのコードは取り込まない(L44-48)。休眠プロジェクトなら検討される(L50-53)。BSD などライセンスが許しても Apache-2.0 以外は望ましくない(L55-57)** |
| nuttx `Documentation/contributing/making-changes.rst` | fork/branch/rebase の手順。PR 前に「通常は 1 コミットに squash」(L189-191)。件名は `subsystem/area: ...`(L241-278)。新規ファイルは `.github/CODEOWNERS` に自分を足してよい(L288-298)。`.gitmessage` をコミットの雛形に使える(L229-238) |
| nuttx `Documentation/contributing/coding_style.rst` | NuttX C 規約(CONTRIBUTING 2.1 が必須と明記)。ファイル見出しは相対パス + Apache-2.0 文面(L45-53)。**第三者のファイルは見出しにそのライセンスを書き、トップの `LICENSE` / `NOTICE` に NOTE を足す(L56-66)** |
| nuttx `Documentation/contributing/documentation.rst` | 文書のビルド手順(`pipenv` + `make html`、plantuml)と RST の書式(字下げ 2、見出しの記号) |
| nuttx `Documentation/contributing/releases.rst` | リリース手順(本件の提出には関係しない) |
| nuttx `.gitmessage` | コミットの雛形:`maintopic/subtopic: Short, explanatory context about this commit.`、`git commit -s` を忘れない |
| nuttx `.github/PULL_REQUEST_TEMPLATE.md`(apps も同文) | Summary / Impact / Testing の 3 節。「**テスト情報の無い PR は受け付けない。ログを求める**」(L32-33) |
| nuttx `.github/workflows/check.yml`(apps も同等) | PR ごとに `checkpatch.sh -c -u -m -g <base>..HEAD`(L52-53)。`Documentation/**` だけの PR では走らない(L17-18)。`breaking change` ラベルで `-b`。apps は **nuttx master の** checkpatch/nxstyle で検査 |
| nuttx `tools/checkpatch.sh` | `-m`:件名に `:`、**件名 80 文字以内**(L292)、2 行目は空行、`Signed-off-by` 必須(L362-365)、最低 5 行、`Change-Id`/`WIP:` 禁止。`-c` codespell、`-u` UTF-8。`Assisted-by` の書式は**機械検査していない** |
| nuttx `.github/workflows/build.yml` + `arch.yml` + `.github/scripts/depends_on.py`(apps も同等) | Linux の build matrix(`cibuild.sh -c -A -N -R -S testlist/<group>.dat`、L395)。ラベルが「Arch/Board + Size だけ」のときだけ絞り込み、それ以外は**全グループ**(arch.yml L147/L170)。本件の PR は Area ラベルが付くので全グループになる。macOS / msys2 / msvc / OOT のジョブもある(L417-616)。**PR 本文の `depends-on:` 行**を読み、その PR を cherry-pick してからビルドする(L122、depends_on.py L45-52) |
| nuttx `tools/ci/cibuild.sh` / `tools/testbuild.sh` / `tools/ci/testlist/*.dat` | `-e "-Wno-cpp -Werror"`(cibuild L150)。testbuild は**ビルド後に `tools/refresh.sh --silent` で defconfig が正規形か、木が汚れていないかを検査**(L429-450)。sim:wireguard は `sim-03.dat`(`/sim/*/*/*/[o-z]*`)に入る |
| nuttx `.github/workflows/doc.yml` | `Documentation/**` を触る PR で python 3.10、`pipenv sync`、`pipenv run make html`(L43-51)。`Documentation/Makefile` の `SPHINXOPTS` に `-W`(L27) |
| nuttx `.github/workflows/lint.yml` | super-linter(Python black/flake8/isort、YAML)。本件は .py/.yml を足さないので対象外 |
| nuttx `.github/labeler.yml` / `pr_labeler.yml` | パスで Area/Arch/Board ラベル、差分量で Size ラベル(CI の選択に使う) |
| nuttx `.pre-commit-config.yaml` / `.editorconfig` | 任意。末尾空白・改行、cmake-format、`checkpatch.sh -c -f` |
| ASF <https://www.apache.org/legal/generative-tooling.html> | §6.1 (a) |

### 6.1 規則ごとの照合

**(a) 生成 AI(CONTRIBUTING.md 1.5 L97-149、1.6 L152-163)**

- 生成ツール/AI を開発・コミットメッセージ作成などに使った**全コミット**に `Assisted-by:` が必須(L114-117)。
  使っていなければ省略可。ASF の `Generated-by:` 推奨を `Assisted-by` に読み替えたもの(L117-121)
- 位置は**署名(`Signed-off-by`)の上**(L111-112:「5. Signature」「6. `Assisted-by: ` above the signature」)
- 書式 `Assisted-by: AGENT_NAME:MODEL_VERSION [TOOL1] [TOOL2]`(L126-134)。AGENT_NAME はツール名、
  MODEL_VERSION は具体的なモデル版、TOOL は任意の専門解析ツール。例 `Assisted-by: Claude:claude-3-opus coccinelle sparse`(L135)
- **AI は `Signed-off-by` を付けてはならない。人だけが法的に証明できる**(L123-124)
- 1.6:必須項目は topic / description / signature / `Assisted-by:`(該当時)。無いものは自動で差し戻し
- PR 本文での開示は**規則上は求められていない**(テンプレートにも欄が無い)
- ASF(2026-08 更新):ツールの利用規約が OSD と矛盾しないこと、出力に第三者の素材が無いか許諾があること、などの条件。
  開示はコミットメッセージの `Generated-by: <Name of Tool> <Version>`(NuttX はこれを `Assisted-by:` に置き換えた)。
  人が選択・編集した部分は著作物になりうる
- Linux カーネル <https://docs.kernel.org/process/coding-assistants.html>(1.5 が参照):git・gcc・make・
  エディタなどの基本ツールは書かない。人間の提出者が全コードの確認、ライセンス適合、DCO の署名、全責任を負う
- upstream の実例(2026-06 以降):`Claude Code:claude-opus-5-5` が 131 件、`Claude:claude-opus-5-5` 19 件、
  `Codex:GPT-6` 30 件など。いずれも `Assisted-by:` の次の行が `Signed-off-by:`

**本シリーズで使う文面**(本人の申告:ツールは Claude Code、モデルは「Claude Opus 5.5」。
本人は「かな」と書いており**未確定**。Claude Opus 5.5 の正式なモデル ID は `claude-opus-5-5`):

```
Assisted-by: Claude Code:claude-opus-5-5
Signed-off-by: satoru akita <wwlap24@gmail.com>
```

- **これまでの記録には Codex を使った作業も出てくる**(merge-strategy §3 H1)。Codex も使ったなら、
  そのモデル名を確かめて `Assisted-by: Codex:<MODEL_VERSION>` を**もう 1 行**、Claude Code の行の下・署名の上に足す
  (CONTRIBUTING は複数ツールの書き方を定めていない。1 ツール 1 行は選択)。最終的な一覧は本人が確定する
- 2026-10-12 の件名の書き換え(§6.2)は Claude Code(`claude-opus-5-5`)で行った。コミットメッセージの作成も
  1.5 の対象(「writing commit messages」)なので、少なくとも全コミットに上の 1 行が当てはまる
- PR 本文の任意の開示文(pr-drafts.md の各 Summary 末尾に置き場所がある):

  > Generative AI tooling (Claude Code, claude-opus-5-5) assisted with this change; every commit
  > carries an Assisted-by: trailer. I have reviewed and tested all of it and take responsibility for it.

**(b) コミットメッセージ(CONTRIBUTING.md 1.5 L103-112、2.2 L412-446、`.gitmessage`、checkpatch.sh)**

| 規則 | 出典 | 本シリーズ(`pr-latest/*`、10/12) |
|---|---|---|
| 件名は「領域の接頭辞、`:`、短い自己説明の文脈、`.`」 | 1.5 L105-106、`.gitmessage` L2 | **10/10 版は末尾の `.` が無く、`:` の後が小文字だった → 直した**(例 `crypto/chachapoly: Place the 64-bit nonce in the last eight bytes.`)。upstream の実際は両方混在だが、規則と雛形に合わせた |
| 件名 80 文字以内、次の行は空行 | checkpatch.sh L292, L345-348 | 最長 73 文字(`drivers/net/netdev_upperhalf: Accept IP-only lower halves for WireGuard.`) |
| 本文:何を・どう・なぜ(1.3 の項目) | 1.5 L108-109、1.3 L64-74 | 全コミットにある。試験ログは「PR にあれば省略可」(L100-101) |
| 署名は `git commit -s` で作るもの。必須 | 1.5 L111、1.6 L159 | **未署名(全 12 コミット)**。本人の作業 |
| `Assisted-by:` は署名の上 | 1.5 L112 | **未記入**。本人の作業 |
| 業務用メールアドレス(該当する場合) | 1.16 L365-366 | 作者は `wwlap24@gmail.com`。§6.5 の 4 |
| 実験的な機能は件名と PR 題に `[EXPERIMENTAL]` | 1.11 L253-258 | 付けていない(merge-strategy §6 の 4) |
| breaking change は `!` と `BREAKING CHANGE:` | 1.13 L320-326 | 該当しない(全部 `CONFIG_NET_WIREGUARD` 既定 n の下) |

**(c) PR(CONTRIBUTING.md 1.3 L62-87、1.7 L166-195、1.8 L198-219、1.9 L226-238、2.3 L449-502)**

| 規則 | 出典 | 本シリーズ |
|---|---|---|
| テンプレート(Summary / Impact / Testing / Self-Check)の**全項目**が必須 | 1.7.1 L168-170、2.3 L454-502 | **10/10 の下書きは Self-Check 節が無く、Impact が項目別でなかった → pr-drafts.md を直した** |
| **実機 1 台以上**のビルド・実行ログが必須。QEMU は数えない(1.14 で明記) | 1.3 L76-78、1.7.2 L171-172、1.9 L230-231、1.14 L339-340 | **10/12 の SHA で実機ログは未取得**(§3 (b)) |
| 1 PR 1 機能。別機能は別 PR | 1.7.3-4 L173-176 | 5 PR に分割済み |
| 各コミットが単独でビルド・実行・互換を壊さない | 1.7.5 L177-179 | §6.6 |
| 束ねるしかない場合は「他に方法が無く、breaking change ではない」と明記 | 1.7.9 L184-187 | PR-K1 本文に記載 |
| 新機能は**同じ PR に**文書。apps のコードの文書は nuttx に別 PR | 1.7.7 L182、1.8 L202-212 | PR-K1 に wireguard.rst、apps の文書は PR-D |
| 文書のビルドログは歓迎 | 1.8 L217 | §6.6 |
| WIP は `[WIP]` とドラフト | 1.7.8 L183 | 該当しない |
| 依存は本文で示す | 1.3 L73-74 | **CI が読むのは行頭の `depends-on: <owner>/<repo>/pull/<N>`(または URL)だけ。`#1234` や `Depends on #1234` は読まない**(depends_on.py L14-21, L45-52)。10/10 の下書き(`Depends on #NNNN`)を直した |
| push のたびに CI 一式が走る。完成したものだけを出す | 1.7 L192-195 | merge-strategy §5 のとおり |

### 6.2 `pr-latest/*` に行った変更(2026-10-12、ローカルのみ)

署名・`Assisted-by:` は**付けていない**。push もしていない。10/10 版は `bak/pr-latest-20261010/*`
(nuttx 4 本、apps 1 本)として残した。

1. **upstream 先端に載せ替え**(nuttx `0916f1a561` → `4bc1ab2fd7`、17 コミット先。apps `da92a492b` → `74df79a53`)。
   競合なし。間の upstream で本シリーズのファイルに重なるのは `tools/nxstyle.c`(+14 行、別の配列)だけ
2. **件名を 1.5 の形に**:`:` の後を大文字、末尾に `.`。本文・作者・作者日時は変えていない
   (`git range-diff` で差は件名だけ)
3. **sim:wireguard の defconfig を正規形に**(`boards/sim: Add the sim:wireguard configuration.` の木だけ変更):
   `CONFIG_NETUTILS_PING=y` と `CONFIG_NSH_LINELEN=256` を削除。CI の `testbuild.sh` は `refresh.sh` で defconfig を
   正規化し、差が出ると「木が汚れた」として FAIL にする。この 2 行は他の選択から決まる値で、`savedefconfig` が落とす
   (§6.6 の 1 回目で FAIL を確認)

| PR | ブランチ | コミット(古い順) |
|---|---|---|
| PR-C | `pr-latest/crypto-chachapoly` | `a62e8dbf54` crypto/chachapoly: Place the 64-bit nonce in the last eight bytes.<br>`5d66c8901e` crypto/testmngr: Add ChaCha20-Poly1305 and XChaCha20-Poly1305 KATs. |
| PR-N | `pr-latest/nxstyle-wg` | `66218ee3e7` tools/nxstyle: Skip the vendored X25519 in system/wg. |
| PR-K1 | `pr-latest/wireguard-driver` | `73474ea80c` net: Fix nxstyle issues in netdev_upperhalf.c and netdev_register.c.<br>`52be44b168` drivers/net/netdev_upperhalf: Accept IP-only lower halves for WireGuard.<br>`31231d4965` net/netdev: Add the WireGuard control ioctls.<br>`486489718c` drivers/net/wireguard: Add the WireGuard device.<br>`2901802758` boards/sim: Add the sim:wireguard configuration.<br>`0b4ed6ec77` Documentation: Add the WireGuard network device. |
| PR-D | `pr-latest/doc-system-wg` | `237b6e538e` Documentation: Add the system/wg command. |
| PR-A | apps `pr-latest/system-wg` | `3736813544` system/wg: Add a WireGuard configuration command.<br>`fc640afc4c` LICENSE: Add the STROBE X25519 used by system/wg. |

`pr/*`(10/07 base)は触っていない。`pr/*` の sim:wireguard defconfig も同じ 2 行を持つので、`pr/*` から出すなら同じ修正が要る。

### 6.3 見つかった差(ギャップ)

| # | 差 | 根拠 | 状態 |
|---|---|---|---|
| R1 | `Assisted-by:` が無い | 1.5 L114-117 | 本人(文面は §6.1 (a)) |
| R2 | `Signed-off-by:` が無い | 1.5 L111、1.6 L159、checkpatch L362-365 | 本人(§6.4 のコマンド) |
| R3 | 件名の形(末尾 `.`、大文字) | 1.5 L105-106、`.gitmessage` | **直した**(§6.2) |
| R4 | sim:wireguard の defconfig が正規形でない → CI の Linux ビルドが FAIL | testbuild.sh L429-450 | **直した**(§6.2) |
| R5 | **PR-K1 を apps master で CI にかけると sim:wireguard が FAIL**:defconfig の `CONFIG_SYSTEM_WG=y` / `CONFIG_SYSTEM_WG_CONFIG_PATH` は apps の PR-A が入るまで存在しない Kconfig 記号で、`refresh.sh` が消す。PR-K1 のマージ後・PR-A のマージ前は、sim-03 を回す他人の PR の CI も同じ理由で落ちる | build.yml(apps master を checkout)、testbuild.sh L432 | **判断待ち**(§6.5 の 2) |
| R6 | 依存の書き方。CI は `depends-on:` 行だけを読む | depends_on.py L14-21, L52 | pr-drafts.md を直した |
| R7 | PR テンプレートの Self-Check 節・Impact の各項目が下書きに無い | 2.3 L454-502 | pr-drafts.md を直した |
| R8 | 実機ログ(10/12 の SHA で未取得) | 1.7.2、1.9 | 本人(§3 (b)) |
| R9 | `wg_noise.c` は**開発が続いている** wireguard-lwip(BSD-3)由来。workflow.rst は活発なプロジェクトのコードは取り込まない、Apache-2.0 以外は望ましくない、としている | workflow.rst L44-57 | **判断待ち**(§6.5 の 3)。wireguard-lwip の最新コミットは 2026-08-14(GitHub API で確認) |
| R10 | 会社員の場合の著作権・契約、業務用メール | workflow.rst L38-40、1.16 L365-366 | **判断待ち**(§6.5 の 4) |
| R11 | making-changes.rst は「PR 前は通常 1 コミットに squash」と書く。本シリーズは PR 内を分けている | making-changes.rst L189-191。CONTRIBUTING 1.7.5 L177-179 は複数コミットを認める | 規則(CONTRIBUTING)上は可。レビューで squash を求められたら従う |
| R12 | `wg_x25519.c` に STROBE 由来の誤記 `x1 = (const uint8_t *)x1;`(本来 `x1i`)。`X25519_MEMCPY_PARAMS 1` の構成では、整列済みの写しを作ったのに整列していない入力を読み続ける。wireguard-lwip が 2026-08-14 に同じ箇所を直した(PR #17、ARMv6-M で unaligned access の例外) | apps `system/wg/wg_x25519.c` L281 | **判断待ち**(§6.5 の 5)。CI の規則違反ではない |
| R13 | macOS / msys2 / msvc の CI ジョブ、他アーキテクチャのグループは再現していない | build.yml L471-616 | 未実施(§6.6) |

### 6.4 署名の手順(直したもの)

10/10 版(§3 (a))からの変更点:base を 10/12 の載せ替え先に、`Assisted-by:` を §6.1 (a) の文面に、
Codex 用の 2 行目を任意で。`git commit --amend --no-edit --trailer A --trailer S` は**与えた順に**末尾へ足すので、
`Assisted-by:` が署名の上に来る(同じ引数の `git interpret-trailers` をメッセージ本文に当てて確認。§6.6)。
`git commit -s` は署名を先に置くので使わない。

```bash
# 本人が確定してから実行する(Codex も使ったなら AB2 の 2 行を有効にする)
AB='Assisted-by: Claude Code:claude-opus-5-5'
# AB2='Assisted-by: Codex:<MODEL_VERSION>'
SOB='Signed-off-by: satoru akita <wwlap24@gmail.com>'   # 名義を変えるならここ(§6.5 の 4)
SIGN="git commit --amend --no-edit --trailer \"$AB\" --trailer \"$SOB\""
# SIGN="git commit --amend --no-edit --trailer \"$AB\" --trailer \"$AB2\" --trailer \"$SOB\""

cd /c/Users/0000400096/workspaces/nuttx
git status --short                     # 空であること
git fetch upstream                     # 先端が動いていたら先に載せ替える(merge-strategy §5)
git switch pr-latest/doc-system-wg
git rebase --update-refs --exec "$SIGN" 4bc1ab2fd7   # PR-C / K1 / D の 3 本が書き換わる
git switch pr-latest/nxstyle-wg
git rebase --exec "$SIGN" 4bc1ab2fd7
git switch master

cd /c/Users/0000400096/workspaces/nuttx-apps
git switch pr-latest/system-wg
git rebase --exec "$SIGN" 74df79a53
git switch master
```

確認:

```bash
cd /c/Users/0000400096/workspaces/nuttx
git range-diff 4bc1ab2fd7..237b6e538e 4bc1ab2fd7..pr-latest/doc-system-wg   # 差はメッセージ末尾の trailer だけ
git log --format='%h %(trailers:key=Assisted-by,key=Signed-off-by,separator=%x20|%x20)' 4bc1ab2fd7..pr-latest/doc-system-wg
./tools/checkpatch.sh -c -u -m -g 4bc1ab2fd7..pr-latest/doc-system-wg       # CI と同じ。Missing Signed-off-by が消えること
```

### 6.5 本人が決めること(merge-strategy §6 と本書 §5 に追加)

1. **`Assisted-by:` の最終的な一覧**:Claude Code の行のモデル(`claude-opus-5-5` でよいか)、Codex を使ったか・そのモデル
2. **R5 の順序問題**。考えられる形(どれも未実施):
   - (i) PR-K1 の sim:wireguard から `CONFIG_SYSTEM_WG*` を外し(driver だけをコンパイルする構成)、PR-A のマージ後に
     PR-D で足す。PR-D は Documentation だけでなくなるので build/check ジョブも走る
   - (ii) PR-K1 の本文に `depends-on: apache/nuttx-apps/pull/<PR-A>` を書き、PR の CI には PR-A を当てさせる。
     ただし PR-A は PR-K1 の ABI に依存するので相互依存になり、片方が先にマージされた時点で master 側の CI が落ちる
     (1.12 の「nuttx と nuttx-apps を同時に変える」に当たりうる)
   - (iii) (i) と同じで、wg 付きの構成は PR-A 後の独立した小さな PR にする
3. **R9**:wireguard-lwip 由来の `wg_noise.c` を BSD-3 のまま入れることの説明(workflow.rst L44-57)。
   dev@ の論点に入れるか、PR-K1 本文にどう書くか
4. **R10**:作者・署名のメール(`wwlap24@gmail.com` のままか)、所属組織との著作権・契約の確認(workflow.rst L38-40)
5. **R12**:`wg_x25519.c` L281 の誤記を PR-A で直すか(直すと「STROBE の本体は 1 文字も変えていない」と書けなくなる)
6. 出す系統を `pr-latest/*`(10/12)にするか。`pr/*` は R3・R4 が未修正

### 6.6 CI の再現(2026-10-12、Docker)

環境:`nuttx-wg-ci:latest`(Ubuntu 24.04、gcc 13.3、riscv64-unknown-elf-gcc、codespell/cvt2utf/black/isort/
flake8/cmake-format)に curl/unzip を足したもの。ローカル clone を `/src` に読み取り専用で mount し、中で
`git clone --shared`。公式 CI イメージ(`ghcr.io/apache/nuttx/apache-nuttx-ci-linux`)ではない。スクリプトは
scratchpad の `ci1012/`(setup.sh / check.sh / build.sh / percommit.sh / run3.sh)。**どれもコミットを変更しない。**

**check.yml の再現**(PR ごとに CI と同じ `checkpatch.sh -c -u -m -g <base>..HEAD`):

| PR | 範囲 | 結果 |
|---|---|---|
| PR-C | `4bc1ab2fd7..pr-latest/crypto-chachapoly`(2) | `Missing Signed-off-by` ×2 だけ。codespell・UTF-8・nxstyle の指摘なし |
| PR-N | `..pr-latest/nxstyle-wg`(1) | 同上 ×1 |
| PR-K1 | PR-C に積んだまま(8)/PR-C マージ後(6) | 同上 ×8 / ×6 |
| PR-D | `pr-latest/wireguard-driver..`(1) | 同上 ×1(実際の CI では Documentation だけの PR なので check は走らない) |
| PR-A | nuttx master の checkpatch | **`wg_x25519.c` で nxstyle エラー多数**(PR-N が前提であることの確認) |
| PR-A | PR-N を入れた nuttx の checkpatch | `Missing Signed-off-by` ×2 だけ |

**署名後のメッセージ**:全 12 コミットの本文に、§6.4 と同じ引数の `git interpret-trailers`
(`Assisted-by: AGENT_NAME:MODEL_VERSION`、`Signed-off-by: AuthorName <Valid@EmailAddress>` の仮の値)を当て、
`checkpatch.sh -m -g --stdin` に通した:**12 件とも `All checks pass`**、末尾は `Assisted-by:` → `Signed-off-by:` の順。
コミットは作っていない。

**build.yml の再現**(`cd tools/ci && ./cibuild.sh -A -N -R -S <list>`。cibuild が `-e "-Wno-cpp -Werror"` を付け、
testbuild が defconfig の正規形と木の汚れを検査):

| 回 | nuttx / apps | 構成 | 結果 |
|---|---|---|---|
| 1 | PR-C 先端 `5d66c8901e` / apps master | sim:crypto, sim:nsh | **PASS**(rc=0、208 秒) |
| 1 | PR-K1 先端(defconfig 修正前 `bcbcab3376`)/ apps master | sim:wireguard | **FAIL**:refresh で `CONFIG_NETUTILS_PING=y`、`CONFIG_NSH_LINELEN=256`、`CONFIG_SYSTEM_WG=y`、`CONFIG_SYSTEM_WG_CONFIG_PATH` が消え、木が汚れる(4 回再試行して同じ)。R4 と R5 |
| 1 | PR-D 先端(修正前 `e84831c5f1`)/ PR-A `fc640afc4` | sim:wireguard ほか | **FAIL**:apps に system/wg があっても `NETUTILS_PING` と `NSH_LINELEN` の 2 行が消える(R4)。木が汚れたまま次の構成に進むので、同じ実行の後続構成の結果は使えない |
| 3 | PR-D 先端 `237b6e538e`(修正後)/ PR-A `fc640afc4` | sim:wireguard, sim:crypto, sim:nsh, sim:nettest, sim:usrsocktest, sim:tcpblaster, sim:vpnkit, CMake の sim:nsh / nettest / usrsocktest, rv-virt:netnsh64, rv-virt:knetnsh64 | **sim の 7 構成と CMake の 3 構成はすべて 1 回目で PASS**(defconfig の正規形・木の汚れの検査を含む)。rv-virt:netnsh64 は `math.h: No such file or directory` で FAIL(このイメージの riscv64-unknown-elf に libc/libm のヘッダが無い。本シリーズの変更とは関係しない環境の差。公式 CI イメージでは未確認)。そのため rv-virt:knetnsh64 の結果は無い |
| 3 | PR-K1 先端 `0b4ed6ec77` / apps master | sim:wireguard | **FAIL(確認済み)**:refresh で消えるのは `CONFIG_SYSTEM_WG=y` と `CONFIG_SYSTEM_WG_CONFIG_PATH` の 2 行だけ(R5)。R4 の 2 行は消えない |
| 3 | コミットごとの `-Werror` ビルド(コードを含む 10 コミット:PR-C 2、PR-K1 6、PR-A 2。PR-N と PR-D は対象外) | §2.2 と同じ | **全 10 件 PASS、警告 0**。PR-C の 2 件目は起動時の KAT も PASS。PR-K1 のデバイス追加以降の 3 件は、apps の system/wg なしで `wg0` が登録されることも確認 |
| 3 | sim 一式・CMake・BUILD_KERNEL(`tip.sh`、nuttx `237b6e538e` / apps `fc640afc4`) | §2.3 と同じ | **すべて PASS**:sim:wireguard ビルド(警告 0)、sim 試験一式 8 本(369 秒)、CMake/Ninja(T8)、rv-virt:knetnsh64 の BUILD_KERNEL ビルドと実行(Linux の WireGuard と双方向、負荷中の再設定と down/up から復帰) |

**doc.yml の再現**(`python:3.10`、`pipenv sync`、`pipenv run make html`。`SPHINXOPTS` に `-W`):
PR-D 先端(修正前 `e84831c5f1`)**PASS**(303 秒、`applications/system/wg/index.html` と
`os/drivers/special/net/wireguard.html` を生成)、PR-K1 先端(修正前 `bcbcab3376`)**PASS**(332 秒)。
defconfig の修正は `Documentation/` を変えないので、修正後の SHA でも文書の木は同じ。

**再現していないもの**:公式 CI イメージでのビルド、他アーキテクチャのグループ(arm / xtensa / arm64 など。
ESP32-S3 の xtensa ツールチェーンはこのイメージに無い)、macOS / msys2 / msvc / OOT のジョブ、codechecker、
lint.yml(対象ファイルなし)、実機。
