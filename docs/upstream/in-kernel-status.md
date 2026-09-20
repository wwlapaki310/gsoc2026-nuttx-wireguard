# カーネル移行 実装状況メモ

最終更新: 2026-09-20。計画本体は [in-kernel-plan.md](in-kernel-plan.md)、追跡は
[#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11)。

## 一言で

**`drivers/net/wireguard/` のカーネル版が sim で完動**(Linux カーネル WireGuard と
双方向ハンドシェイク・トンネル ping・replay/cookie 否定系すべて PASS)。
**BUILD_KERNEL(qemu-armv7a:knsh)でもビルド成功**——apps/kernel 分離がクリーンな証拠。
実機(T5)はマンションのネット回線障害で Wi-Fi 不通のため保留。

## リポジトリの状態(未 push、ローカル fork)

| 場所 | ブランチ | HEAD | 中身 |
|---|---|---|---|
| `C:\Users\wwlap\workspace\nuttx`(fork `wwlapaki310/nuttx`) | `net-wireguard` | `d5f59c9d06` | **機能単位 2 コミットに整頓済み**: (a) `213cd66095` crypto/chachapoly nonce 単独 → (b) `d5f59c9d06` net/wireguard(統合点・ABI・driver・crypto shim・protocol core・sim:wireguard defconfig・Documentation)。Signed-off-by / SPDX 済み |
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

## 済み(2026-09-20 追加分)

- ~~fork のコミット整頓~~ **完了**: nuttx を 2 コミット(chachapoly 単独 → net/wireguard)、apps を 1 コミットに。Signed-off-by / Co-Authored-By 付与。squash 後に再ビルド(`BUILD_EXIT=0`)・runtime・replay 全 PASS で退行なしを確認。presquash バックアップタグ `backup/net-wireguard-presquash` / `backup/system-wg-presquash`
- ~~`wg_x25519.c` の第三者扱い~~ **完了**: `wg_x25519.c` / `.h` に NuttX 標準ブロック + `SPDX-License-Identifier: MIT` + `SPDX-FileCopyrightText` + MIT 全文
- ~~Documentation~~ **完了**: `Documentation/components/drivers/special/net/wireguard.rst`(+ index.rst の toctree)、`Documentation/applications/system/wg/index.rst`。いずれも nuttx リポジトリ側、commit (b) に同梱

## 残タスク

1. **S4a 実行時**: `qemu-armv7a:knetnsh`/`knsh` を QEMU で起動し、ROMFS 上の `wg` ELF で set→up→Linux ピアとハンドシェイク→ping(BUILD_KERNEL の実行時証明)。ビルドは通ったので次は TAP + ROMFS 配線
2. **実機(T5)**: Wi-Fi 復旧後。kernel 版イメージを ESP32-S3 / Spresense に焼いて runtime 確認(現状は apps 版 v0.1.1 が入っている)
3. **push + PR**: 本人が実施。順序は plan §4.1(#9 → chachapoly → dev@/Issue → PR-K1 → PR-A1)

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

`kdev.sh sync` は各 fork の `git diff merge-base(HEAD, upstream/master)` を当てるので、
**fork でコミット済み**の変更だけが反映される(未 add の新規ファイルは入らない — defconfig で一度これに嵌まった)。
