# カーネル移行 実装状況メモ

**現在の残件・作業順（2026-09-28）:** [remaining-work.md](remaining-work.md)。
実測の正本は [verification-matrix.md](verification-matrix.md)、公開報告は
[Discussion #18](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/discussions/18)。

**2026-09-28 現在の検証範囲。** sim に加えて次が**実行**済み:
BUILD_KERNEL(`rv-virt:knetnsh64`、syscall 境界)、**BUILD_PROTECTED(`rv-virt:pnsh64`、
MPU 分割)**、**SMP 4 CPU(`rv-virt:knetnsh64_smp`)**、実機 ESP32-S3 と Spresense
(GS2200M = usrsock、故障経路の強制を含む)。さらに実機で**資源を測定**し、
`wg_rx` スレッドのスタック高水位は **1472 B / 6144 既定**。
「動くトンネルでは検出できない」種類の故障も個別に潰した(定数シードによる鍵の固定、
down 後のセッション鍵残存、時間が経ってから出る漏れ)。

**TAI64N（#14）は「判断として」OPEN。** 実時刻 + 起動内高水位を採用し（Linux /
wireguard-go と同じ契約）、uptime へフォールバックしない。時計が未設定なら 1 度だけ
警告して結果を明示する。2026-09-28 に**実機で対照実験として測定完了**:
ボードへ何も送らない条件で、時計未設定なら 75 秒経ってもハンドシェイク成立せず、
時計を設定すると 4.1 秒で成立。耐久性のある範囲予約は [tai64n-design.md](tai64n-design.md)
に設計済みだが**意図的に未実装**（NuttX に保存の耐久性契約が無い）。
比較と判断は [tai64n-decision.md](tai64n-decision.md)、実機の証跡は
[evidence/spresense-timestamp-2026-09-28.md](evidence/spresense-timestamp-2026-09-28.md)。

**並行性設計:** queued-output / `d_lock` は `66b7403c8a` に取り込み済みで、
rv-virt BUILD_KERNEL・BUILD_PROTECTED・SMP 4 CPU・実機 usrsock の故障強制で確認済み。
[並行性修正・検証記録](locking-followup.md) と [設計文書](in-kernel-design.md) を参照。

**fork HEAD:** `net-wireguard` `b230ee4876` / `system-wg` `691311a4`
（base upstream/master `c95c546c`）。いずれも upstream 未 push（本人作業）。

現状案内の更新: 2026-09-28。計画本体は [in-kernel-plan.md](in-kernel-plan.md)、追跡は
[#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11)。

## 一言で

**`drivers/net/wireguard/` のカーネル版が sim で完動**(Linux カーネル WireGuard と
双方向ハンドシェイク・トンネル ping・replay/cookie 否定系すべて PASS)。さらに
**実カーネルビルド(`rv-virt:knetnsh64`、BUILD_KERNEL + virtio-net)を QEMU で起動し、
実 Linux カーネル WireGuard 相手に双方向トンネル + ping が成立(S4a 完了)**。
qemu-armv7a:knsh の BUILD_KERNEL ビルドも成功(apps/kernel 分離がクリーンな証拠)。
実機(T5)は9月26日にESP32-S3とSpresense（usrsock）で通常通信PASSの報告あり。
両ボードのFLAT構成であり、実機のBUILD_KERNEL/PROTECTED試験とは別です。

**検証 story(upstream 向け):** ① CI コンパイル経路 `sim:wireguard` defconfig、
② sim ランタイム + replay/cookie を実 Linux WG 相手に(protocol/crypto 正しさ)、
③ **`rv-virt:knetnsh64` の実カーネルビルドで実ネットワーク経由の完全トンネル**
(syscall 境界越しの driver + 別 ELF の wg + virtio netdev)。

## 履歴: 9月20日時点のリポジトリ・詳細記録

以下のHEAD・提出状況・詳細一覧は当時の記録です。現在の状態は冒頭のリンクと
各リポジトリのgit状態で確認し、古いSHAや残件を現在の状態として引用しないでください。

| 場所 | ブランチ | HEAD | 中身 |
|---|---|---|---|
| `C:\Users\wwlap\workspace\nuttx`(fork `wwlapaki310/nuttx`) | `net-wireguard` | `66b7403c8a` | **機能単位 2 コミットに整頓済み**: (a) `213cd66095` crypto/chachapoly nonce 単独 → (b) `66b7403c8a` net/wireguard(統合点・ABI・driver・crypto shim・protocol core・sim:wireguard defconfig・Documentation。**BUILD_KERNEL スタックオーバーフロー修正込み**)。Signed-off-by / SPDX 済み |
| `C:\Users\wwlap\workspace\nuttx-apps`(fork `wwlapaki310/nuttx-apps`) | `system-wg` | `24b3f311` | `apps/system/wg`(ioctl クライアント、1 コミット、reword + `wg_x25519.c/.h` に MIT SPDX ヘッダ付与) |
| `gsoc2026-nuttx-wireguard` | `main` | `3cbf1bd`(push 済み) | `scripts/kdev.sh`、`scripts/kernel/verify-*.sh`、ドラフト各種 |

両 fork とも **upstream には未 push**(役割分担: upstream への push/PR は本人)。
両 fork の base は upstream/master `c95c546c`。ローカルでは `git rebase upstream/master` で追従。

## できたもの(sim で検証済み)

- `include/nuttx/net/wireguard.h` — フラット固定長 ioctl ABI(`SIOCSWGIF`/`SIOCGWGIF`/`SIOCSWGPEER`/`SIOCDWGPEER`/`SIOCGWGPEER` = 0x0046〜0x004A)
- `drivers/net/wireguard/` — `wireguard.c`(netdev_lowerhalf、UDP ソケット + RX kthread、timers、cookie、ioctl、`wireguard_initialize()`)/ `wg_noise.c`(Noise_IKpsk2 本体、replay 窓 2048、レート制限修正)/ `wg_crypto.c`(NuttX `crypto/` の薄い層)
- `apps/system/wg` — `up/down/show/showconf/setconf/saveconf/set/genkey/pubkey`。秘密鍵は driver が返さないので設定ファイルを正本にする方式。X25519 は MIT を vendored(`wg_x25519.c`)
- `boards/sim/sim/sim/configs/wireguard/defconfig` — CI がコンパイルする経路。`./tools/configure.sh sim:wireguard` でクリーンビルド
- 検証(`scripts/kernel/`):
  - `verify-sim-wg-runtime.sh` — 鍵なし up 拒否 / on-device genkey / 不正 peer を残さない / pubkey が wg(8) と一致 / traffic / saveconf / down 停止 / setconf 復元 → **全 PASS**
  - `verify-sim-wg-replay.sh` — 再送で endpoint が動かない / 洪水に cookie reply / トンネル生存 → **全 PASS**
- checkpatch: 自作ファイルはクリーン(第三者 `wg_x25519.c` を除く)
- **BUILD_KERNEL**: `qemu-armv7a:knsh` + NET_WIREGUARD + SYSTEM_WG で `make` 成功(driver・crypto・wg ELF がカーネル/ユーザー分離下でコンパイル・リンクできる = 移行の主目的の達成)。knsh には `CONFIG_NET_LL_GUARDSIZE 32` と DEVICE_TREE/LIBC_FDT/DEV_SIMPLE_ADDRENV が要る

## 途中で見つけた NuttX 本体のバグ(重要)

**`crypto/chachapoly.c` の `chacha20poly1305_encrypt/decrypt(u64 nonce)` が counter を nonce の bytes 0..7 に置く**が、RFC 8439 / WireGuard は bytes 4..11。counter 0 は一致するので**ハンドシェイクは通るがデータ 2 個目以降が全滅**。in-tree に利用者がおらず未検証だった。fork で 2 行修正(`memcpy(le_nonce_array + 4, ...)`)して解決。ドラフト: [chachapoly-nonce-draft.md](chachapoly-nonce-draft.md)。**WireGuard driver PR の前に出す `crypto:` PR**にする。今は driver コミットに同梱されているので、PR 整理時に分離する。

これで upstream ドラフトは 4 本: RTC_HIRES(#9)、GS2200M(#10)、chachapoly nonce(この件)、それと WireGuard 本体。

### BUILD_KERNEL スタックオーバーフロー(S4a で発見・修正済み)

rv-virt:knetnsh64 のブリングアップ中に `wg set private-key` で NuttX が panic
(`Store/AMO access fault` → `mm_delayfree` でヒープ破損)。原因は `wg_set_if()` が
鍵変更時にピア設定を退避する `struct wg_peer_s saved[WG_MAX_PEERS]` を**スタックに**
確保していたこと。`sizeof(wg_peer_s)=1512`、4 ピアで **6048 bytes** となり、
BUILD_KERNEL の kernel stack(3072 bytes)を溢れて隣接ヒープを破壊していた。
sim(FLAT、大きいタスクスタック)では露見しなかった。`saved` を
`kmm_malloc`/`kmm_free` でヒープに移して解決(driver コミット `66b7403c8a` に同梱)。
カーネルドライバが数 KB のスタックを要求しないための正しい修正で、upstream にも価値がある。

## 済み(2026-09-20 追加分)

- ~~fork のコミット整頓~~ **完了**: nuttx を 2 コミット(chachapoly 単独 → net/wireguard)、apps を 1 コミットに。Signed-off-by / Co-Authored-By 付与。squash 後に再ビルド(`BUILD_EXIT=0`)・runtime・replay 全 PASS で退行なしを確認。presquash バックアップタグ `backup/net-wireguard-presquash` / `backup/system-wg-presquash`
- ~~`wg_x25519.c` の第三者扱い~~ **完了**: `wg_x25519.c` / `.h` に NuttX 標準ブロック + `SPDX-License-Identifier: MIT` + `SPDX-FileCopyrightText` + MIT 全文
- ~~Documentation~~ **完了**: `Documentation/components/drivers/special/net/wireguard.rst`(+ index.rst の toctree)、`Documentation/applications/system/wg/index.rst`。いずれも nuttx リポジトリ側、commit (b) に同梱
- ~~S4a 実行時証明~~ **完了**: `rv-virt:knetnsh64`(BUILD_KERNEL + virtio-net)を QEMU で起動し、実 Linux カーネル WireGuard 相手に双方向トンネル + ping が成立。上記スタックバグを発見・修正して達成。スクリプト化済み(`scripts/kernel/build-knetnsh.sh`、`verify-knetnsh-wg.sh`)。qemu-armv7a:knsh の BUILD_KERNEL ビルドも `scripts/kernel/build-knsh.sh` にスクリプト化(virtio-net 実行時は qemu-armv7a では未配線 = rv-virt を採用)

## 残タスク

1. **実機(T5)**: Wi-Fi 復旧後。kernel 版イメージを ESP32-S3 / Spresense に焼いて runtime 確認(現状は apps 版 v0.1.1 が入っている)
2. **push + PR**: 本人が実施。順序は plan §4.1(#9 → chachapoly → dev@/Issue → PR-K1 → PR-A1)

## 開発ループ(再開用)

```bash
# コンテナ wgdev（nuttx-wireguard:sim-master ベース、/opt/nuttx・/opt/apps は upstream/master）
docker start wgdev
cd gsoc2026-nuttx-wireguard
bash scripts/kdev.sh sync         # 両 fork の diff をコンテナへ
bash scripts/kdev.sh configure    # sim:nsh + NET_WIREGUARD + SYSTEM_WG
bash scripts/kdev.sh build
bash scripts/kdev.sh test kernel/verify-sim-wg-runtime.sh 30
bash scripts/kdev.sh test kernel/verify-sim-wg-replay.sh 30
```

BUILD_KERNEL のビルド/実行時(コンテナに RISC-V toolchain + qemu-system-riscv64 導入済み):

```bash
# ビルド証明（apps/kernel 分離）
docker exec wgdev bash /tmp/build-knsh.sh        # qemu-armv7a:knsh をビルド
# 実行時トンネル（rv-virt:knetnsh64 + 実 Linux WG ピア）
docker exec wgdev bash /tmp/build-knetnsh.sh     # kernel + wg ELF をビルド
docker exec wgdev bash /tmp/verify-knetnsh-wg.sh # ハンドシェイク + ping
```

rv-virt:knetnsh64 は S-mode ビルドなので QEMU は `-bios`（OpenSBI）が要る(`-bios none` 不可)。
コンソールは 16550 UART=`-serial mon:stdio`、virtio-net は
`-M virt,aclint=on -global virtio-mmio.force-legacy=false -device virtio-net-device,bus=virtio-mmio-bus.0`。
NSH はプロンプト直後の 1 文字を落とすので、コマンド送信は先頭に改行を付ける。

`kdev.sh sync` は各 fork の `git diff merge-base(HEAD, upstream/master)` を当てるので、
**fork でコミット済み**の変更だけが反映される(未 add の新規ファイルは入らない — defconfig で一度これに嵌まった)。
