# 提出チェックリスト(2026-10-10)

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
