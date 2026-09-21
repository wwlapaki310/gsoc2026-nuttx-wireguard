# カーネル側移行計画: `drivers/net/wireguard.c` + `wg` クライアント

最終更新: 2026-09-18(v2、3 名の専門家レビューを反映。§6)。状態: **多くが実装済み**(この文書は
当初の *計画*)。**計画と実装済み/検証済みを混同しないこと** — 実際に何が動き何が未了かは
[in-kernel-status.md](in-kernel-status.md)(現状)と [verification-matrix.md](verification-matrix.md)
(テスト別の正直な状態)が正本。本書に書いた保証・テストは、それらで裏が取れているものだけが「済み」。
関連 Issue: [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3)(dev@)、[#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5)(長時間)、[#6](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/6)(FLAT 前提)、[#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)(RTC_HIRES)、[#10](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/10)(usrsock ioctl)、[#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11)(移行追跡)、[#12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12)(文書・再現・証拠)。

## 0. 結論

**`wg0` の netdev 部分を `apps/` から NuttX カーネル(`drivers/net/wireguard.c`)へ移し、暗号は NuttX 本体の `crypto/` を使い、`wg` コマンドは専用 ioctl 越しの薄いクライアントにする。** Linux(カーネルモジュール + `wg(8)`)と同じ分業。

| | 現行(`apps/netutils/wireguard/`) | 移行後 |
|---|---|---|
| ビルド種別 | FLAT のみ(内部 API 直叩き) | FLAT / PROTECTED / KERNEL |
| 前例 | 「apps が netdev を登録する」前例なし | `drivers/net/tun.c` / `vlan.c` / `rpmsgdrv.c` と同型 |
| 暗号 | vendored `crypto/refc`(`wg_` プレフィックスで衝突回避) | NuttX `crypto/`(blake2s / chachapoly / curve25519 / poly1305)。**初回 PR から** |
| プロトコル本体 | vendored 無改変(byte-identical) | BSD ヘッダ + SPDX を保持して NuttX スタイルに再整形、最小パッチを `patches/` で管理 |
| 内部 API の変化への追従 | 黙って壊れる | `sim:wireguard` defconfig を入れて upstream CI に載せる |
| 秘密鍵 | `.config` / ELF / ユーザー空間に平文 | カーネル内のみ。既定で読み戻し不可 |

「いま楽な A(`depends on BUILD_FLAT`)」はマージ後に必ず B をやり直すことになるので採らない。根拠は [../development/phase4-log.md](../development/phase4-log.md)、[upstream-strategy.md](upstream-strategy.md) §2、§6 のレビュー。

---

## 1. 設計

### 1.1 置き場所と前例

`drivers/net/wireguard.c`(大きくなれば `drivers/net/wireguard/`)。Kconfig は `net/Kconfig` の "Link layer support" メニューに `NET_WIREGUARD`(`NET_TUN` / `NET_VLAN` と並ぶ)。初期化は `drivers/drivers_initialize.c` から `wireguard_initialize()`(`tun_initialize()` / `localhost_initialize()` の隣)。

`net/` 配下で `netdev_register()` を呼ぶコードは存在しない(仮想 netdev は全部 `drivers/net/`)。最も近い前例:

- **`drivers/net/vlan.c`** — 他の netdev の上に載る仮想 netdev、専用 SIOC(`SIOCGIFVLAN`/`SIOCSIFVLAN`)を `netdev_ioctl.c` が `d_ioctl` へ転送、`netdev_lowerhalf` 使用、文書は `Documentation/components/drivers/special/net/vlan.rst`
- **`drivers/net/rpmsgdrv.c`** — カーネル内 netdev が `kthread_create()` と `psock_*()` で自分のソケットを持つ前例。「なぜカーネル部品がソケットを開くのか」への答え

ファイル構成(案):

```
drivers/net/wireguard/
  wireguard.c        netdev(netdev_lowerhalf)・TX/RX・タイマ・ioctl。旧 nuttx-wireguardif.c
  wg_noise.c/.h      ハンドシェイク・セッション(旧 wireguard.c を NuttX スタイルに再整形)
  wg_platform.c      時刻・乱数・TAI64N・負荷判定
  wg_internal.h
include/nuttx/net/wireguard.h   ioctl 番号と構造体(ユーザー空間から見える ABI。標準型のみ)
boards/sim/sim/sim/configs/wireguard/defconfig   CI がコンパイルする唯一の経路
Documentation/components/drivers/special/net/wireguard.rst
```

`d_ifname` は `"wg%d"`(`netdev_register()` は書式文字列として扱う。現行 `"wg0"` は偶然動いている)。`NET_LL_TUN` の扱いは `netdev_register.c` の `#ifdef CONFIG_NET_TUN` を `|| CONFIG_NET_WIREGUARD` にするか `depends on NET_TUN` を残すか、PR で説明する。

### 1.2 制御インターフェース(ioctl)

**フラットな固定長構造体 + `ifname` + サブコマンド。ポインタを含めない。** NuttX の SIOC は例外なくこの型(`ifreq` / `iwreq` / `vlan_ioctl_args`)で、OpenBSD の `wg_data_io`(入れ子ポインタ + 可変長)は (a) NuttX の流儀から外れる、(b) `net_ioctl_arglen()` で転送できない、(c) ユーザー渡しポインタを検証する仕組み(`copyin`)が NuttX に無く、秘密鍵を持つ部品としては confused deputy の口になる、の 3 点で採らない。

```
SIOCSWGIF    設定: 秘密鍵(32B)・listen port・wg0 の IPv4 アドレス/マスク・up/down
SIOCGWGIF    取得: 自分の公開鍵・listen port・up/down・ピア数。秘密鍵は返さない(§1.8)
SIOCSWGPEER  追加/更新: 公開鍵(32B)・PSK(32B, 任意)・endpoint・keepalive・allowed-ips 固定配列
SIOCDWGPEER  削除: 公開鍵で指定
SIOCGWGPEER  取得: index 指定で 1 ピア(統計込み)。列挙用。設定/削除は公開鍵キー
```

- 番号は `include/nuttx/net/ioctl.h` の `0x0046` 以降。`select NETDEV_IOCTL`(`d_ioctl` はこれが無いと存在しない)
- 経路: `ioctl(sock, SIOCxWG*, &req)` → `netdev_ioctl()` → `netdev_ifr_ioctl()` に **case を 2 つ追加**(`vlan` と同じ)→ `netdev_findbyname(req->ifname)` → `d_ioctl`。**`net_ioctl_arglen()` には登録しない**(登録すると usrsock がデーモンへ転送して横取りする。未登録なら usrsock は `-ENOTTY` で素通し — `usrsock_ioctl.c:188-191`)。これを PR 説明に明記する
- **`wg up`/`down`・アドレス設定を `SIOCSIFFLAGS`/`SIOCSIFADDR` に任せない**(標準 ifreq は Spresense の GS2200M に横取りされる。#10 で実証済み)。`SIOCSWGIF` の中でカーネルが `netdev_ifup()`・`d_ipaddr` を書く(現行 `wg_configure_address()` と同じ)
- 入力検証(全部 `net_lock()` 下で一時構造体に検証してから一括コミット): 32 バイト・all-zero 拒否・**自分の公開鍵と同じピア拒否**・低位点は `curve25519()` の戻り値 0 で拒否、mask の連続性、`ip & ~mask == 0` に正規化、**allowed-ips のピア間重複は `-EEXIST`**(現行の先頭一致は cryptokey routing 違反)、endpoint のポート 0 / マルチキャスト拒否、endpoint への経路が `wg0` 自身なら `-ELOOP`、鍵未設定なら up 拒否、keepalive ≤ 65535
- `SIOCGWGPEER` の index は削除で詰まるので、`wg show` は 1 回の列挙中に消えたピアを飛ばす/重複する可能性を許容する(表示用途)。設定・削除は公開鍵キーなのでずれない

### 1.3 インターフェースの生成と寿命

`CONFIG_NET_WIREGUARD=y` で `drivers_initialize()` が `wg0` を **netdev 登録だけ**行う(`CONFIG_NET_WIREGUARD_NINTERFACES`、既定 1)。**UDP ソケットと RX kthread は `d_ifup` で作り `d_ifdown` で壊す**(初期化段階で `psock_socket()` や `kthread_create()` をしない。Spresense の rcS 順序 `gs2200m &` → `wg up` → `denyinet on` とも整合: ソケットは usrsock が生きている間に作る)。

### 1.4 暗号(初回 PR から本体 `crypto/`)

NuttX 13.0.1 の `crypto/` に WireGuard の 4 プリミティブは**全部ある**: `blake2s.c`(`blake2s_init_key` 含む)、`chachapoly.c`(`chacha20poly1305_*` と **`xchacha20poly1305_*`/`hchacha20` も**)、`curve25519.c`、`poly1305.c`。vendored `crypto/refc` と `-D` リネームはカーネル版に持ち込まない(crypto 担当が却下する)。

| vendored | NuttX `crypto/` | 落とし穴 |
|---|---|---|
| `x25519(out,scalar,point,1)` **成功で 0** | `curve25519(out,scalar,point)` **成功で非 0**(Linux 規約) | **戻り値が反転**。マクロで吸収し KAT で検出 |
| `wireguard_blake2s(out,outlen,key,keylen,in,inlen)` | `blake2s(out,outlen,in,inlen,key,keylen)` | **引数順が違う** |
| `chacha20poly1305_encrypt/decrypt` | 同名同シグネチャ(tag 比較は `timingsafe_bcmp`) | 名前衝突が現行 `wg_` の理由。寄せ替えでプレフィックス廃止 |
| `crypto_equal` / `crypto_zero` | `timingsafe_bcmp` / `explicit_bzero` | libc のものに寄せる |
| HMAC / kdf1..3 | 無し | BLAKE2s の上に薄く書いて `wg_noise.c` に残す |

Kconfig: `select CRYPTO`、`select CRYPTO_CURVE25519`(既定ではビルドされない)。`crypto/` 側に足りないものが出たら `crypto: ...` の別 PR を先に出す。

### 1.5 データパス・スレッド・ロック

- **`netdev_lowerhalf` で実装する**: TX = `transmit(netpkt)`、RX = `netdev_lower_rxready()` + `receive()`。現行の `d_iob`/`d_buf` 直接操作と `ipv4_input()` 直呼び(IPv6-only 構成でビルドが壊れる)は廃止
- タイマは `nxsem_tickwait(400 ms)` のポーリングをやめ `wd_start()` / `work_queue()` の遅延実行。スレッドは `task_create()` でなく `kthread_create()`
- UDP は**フェーズ 1 では `psock_*()` のまま**(前例 `rpmsgdrv.c`)。`udp_conn` 直結は求められたら後続 PR
- **ロック規約**: ピア表・keypair・暗号バッファは `net_lock()` 下でのみ触る。ioctl ハンドラ全体を `net_lock()` 下で実行。送信は peer の endpoint 等をローカルに写してから(送信待ちで `net_lock` が一時解放される経路で peer が消えうる)。`wg show`(`SIOCGWGPEER`)もロック下でスナップショット
- down の順序: `ifdown` → `work_cancel` → RX 停止(`nxsem`、ポーリング廃止)→ `psock_close`(現行は `work_cancel` → `ifdown` → `close` で、閉じたソケットへ送信する窓がある)
- 固定 1500 バイトのスクラッチ ×2 と `rxbuf[1500]` のスタック配置は Kconfig 化(`NET_TUN_PKTSIZE` 相当)。受信構造体は `memcpy` で取り出す(Xtensa/ARMv7-M の非整列アクセス)
- `now == 0` を番兵に使う箇所は `now` を 1 始まりにして回避(32 bit ms の wrap と起動直後)
- **cookie 経路を両方向実装する**(§1.7)

### 1.6 `wg` コマンド(`apps/system/wg/`)

netdev を登録しなくなるので `netutils` ではなく `system/`(`system/ping` の前例)。ヘッダ依存は `include/nuttx/net/wireguard.h` のみ。

- サブコマンドは現行のまま(`up` / `down` / `show` / `showconf` / `setconf` / `saveconf` / `set` / `genkey` / `pubkey`)。`wg show` の出力書式は変えない(デモ・スライド・スクリプトが参照)
- base64 / INI / `saveconf` は現行コードを流用。`AllowedIPs`/`Endpoint` のカンマ区切り複数値と `line[128]` の切り詰めは直す
- **`genkey`/`pubkey` は apps 側に小さい X25519 を持つ**(ioctl で任意の秘密鍵を渡して導出させない。「鍵未設定のインターフェースに無関係な鍵を渡す ioctl」は ABI として不自然、PROTECTED で userland は `crypto/` を呼べない)。`pubkey` は秘密鍵を**引数でなく stdin**から(NSH 履歴・`ps` に残さない)
- **PSK** を ABI に最初から入れる。実装が後回しの間は `setconf` で `PresharedKey =` を**拒否**する(現行は黙って無視してハンドシェイクが理由不明で失敗する)
- ヘッドレス運用: rcS は `wg setconf <path>` → `wg up`(引数なしの `wg` = Kconfig からの up は消える)。**T5-1「電源投入だけで上がる」の前提は「一度 `saveconf` 済み」に変わる**

### 1.7 プロトコル本体(旧 `wireguard.c`)への最小パッチ

「無改変」は捨てる(§1.1)。以下を `nuttx_port/patches/` に列挙し、smartalock/wireguard-lwip にも upstream する:

1. `rate_limit = (last_initiation_rx - now) < 500` の**減算逆**(常に false でレート制限が死んでいる)
2. replay 窓 32 → 2048(`uint64_t[32]`)。Wi-Fi の再送で正規パケットを落とす
3. `wireguard_is_under_load()` に `struct wireguard_device *` を渡す(直近 1 秒の initiation 数 > 4 で cookie 要求)
4. `wireguard_random_bytes()` を失敗を返せる形に(vendored は `void`。失敗時ゼロ埋めで続行は「ゼロ鍵で握手」)
5. 受信側の期限判定に `sending_counter` でなく `replay_counter`

glue 側(現行 apps 版のうちに直す、S0.5):

- **復号成功 → `wireguard_check_replay()` → その後で endpoint 更新・`last_rx`・`keypair_update()`**。現行は endpoint 更新が replay 検査より前で、keepalive は検査なし → 認証済みパケット 1 個の再送で endpoint を乗っ取れる
- `MESSAGE_COOKIE_REPLY` を `wireguard_process_cookie_message()` に渡す(現行は捨てている → 負荷中の Linux/Go サーバに永久に繋がらない)。応答側は mac1 → (under_load なら mac2 検査 → 無ければ cookie reply 送って終了)→ DH の順
- `wg_add_peer()` が `allowed_source_ips[0]` しか使わず `inet_pton` 失敗でも `valid = true` にする点

### 1.8 鍵のライフサイクル

- ioctl 受領 → 復号 → `wireguard_device_init()` → 一時コピーを `explicit_bzero`。「staged」に base64 の秘密鍵を持ち続けない(現行 `g_staged.private_key`)
- `d_ifdown` で全 peer の keypair ×3 + handshake 状態を破棄、endpoint を `connect_ip` に戻す。unregister で `explicit_bzero(&priv->wg)`。peer 削除で peer 全体をゼロ化(vendored に deinit が無い)
- **`SIOCGWGIF` は秘密鍵を返さない**(既定)。NuttX に uid が無いので「返す = 全タスクに返す」。B 案の存在意義(PROTECTED で鍵をカーネルに閉じ込める)のため既定は返さない
- `saveconf` との整合: **設定ファイルを秘密鍵の正本にする**。`wg set private-key` は「ファイルに書く + カーネルに押す」、`saveconf` は秘密鍵をファイルから・それ以外を `SIOCGWG*` から集めて書く。`showconf` は `PrivateKey = (hidden)`。デバッグ用に `CONFIG_NET_WIREGUARD_EXPORT_PRIVATE_KEY`(default n)で読み戻しを許す
- PR に添付する実機ログから `.config` を除外する(現行は `NET_WIREGUARD_PRIVATE_KEY` が入りうる)

### 1.9 乱数(Kconfig で強制)

`arc4random_buf()` は `/dev/urandom` が無いと `clock()` のハッシュに**黙って**フォールバックする。`/dev/urandom` の既定バックエンドは `ARCH_HAVE_RNG` の無いボード(cxd56 = Spresense)で **XORSHIFT128(定数シード)**。sim/QEMU の Dockerfile も現状 XORSHIFT。

- `NET_WIREGUARD` は `depends on (DEV_URANDOM_ARCH && ARCH_HAVE_RNG) || (DEV_URANDOM_RANDOM_POOL && CRYPTO_RANDOM_POOL)`。XORSHIFT / CONGRUENTIAL ではビルドできない
- `wg_platform.c` は `arc4random_buf()` を使わず、pool 構成では `up_rngbuf()`、ARCH 構成では `/dev/urandom` の `file_read()` を直接呼ぶ。失敗したら `wg up` を拒否
- ESP32-S3: `esp32s3_rng.c` は HW RNG を `/dev/urandom` に出すが pool には注がない → `DEV_URANDOM_ARCH` 構成を要求(または HW RNG → `up_rngaddentropy()` の小 PR を S0 で)
- Spresense: HW RNG 無し → `CRYPTO_RANDOM_POOL` + `BOARD_INITRNGSEED`(cxd56 は `up_rngaddentropy` を一度も呼ばない)。起動直後の rcS `wg up` では IRQ ジッタが閾値(128 words)に届かない可能性が高い → `wg up` 前に `up_rngreseed()`、`rd_newentr` 不足なら `-EAGAIN` で拒否する hook
- sim/QEMU の Dockerfile を `CRYPTO_RANDOM_POOL` に変える

### 1.10 時刻

- `sys_now` は `clock_systime_ticks()` + `TICK2MSEC`(`CLOCK_MONOTONIC` 経由でも RTC_HIRES に依存しないが、明示する。#9 の回帰が WireGuard に波及しない根拠)
- **TAI64N の単調性**: WireGuard の replay 防止は「initiator の timestamp が単調」を要求する。RTC 無し / エポックに戻るボードは再起動後の initiation を相手(Linux/Windows)が黙って捨て、**ボードが initiator の構成では再起動で繋がらなくなる**(現行 T5-7 が通っているのは Windows 側が initiator になっているから)。`wg_platform.c` は `max(CLOCK_REALTIME, 永続化した最終値 + 1)` を返し、秒単位で丸めた最終値を `wg0.conf` と同じ領域に保存。ナノ秒は tick 粒度に切り捨て(細粒度は指紋)
- responder 側の `greatest_timestamp` は RAM(再起動で消える)。Linux も同じ。明記するに留める

### 1.11 落とすもの

- **12.7.0**: カーネル版は 13.x / master 前提。現行 apps 版をタグ `v0.1.0` で凍結し README に明記
- `CONFIG_NET_WIREGUARD_PRIVATE_KEY` 等の Kconfig 焼き込み(鍵をイメージに置かない)
- `lwip/` スタブヘッダ、`crypto/refc`、`WG_TXWORK` の `nxsem_tickwait` ポーリング、`"wg0"` 固定名

---

## 2. 実装戦略

各ステップは「ビルドが通り、前のステップの検証が全部通る」を完了条件にする。**最小垂直スライス(S2a + S4a)で設計の白黒をつけてから、残りを移す。**

| # | ステップ | 成果物 | 完了条件 |
|---|---|---|---|
| S0 | **前提** | #9 の Issue + 修正 PR(`apache/nuttx`)。#10 の usrsock 側修正も小 PR に。dev@ 投稿 + **`apache/nuttx` に Issue**([dev-list-proposal.md](dev-list-proposal.md) を `drivers/net/` 前提・ioctl ABI 提示・`crypto/` 利用の新文面に差し替え) | #9 がレビューに乗る。Issue にレビュアー(Alan Assis / Xiang Xiao / Zhe Weng / Alin Jerpelea)を CC |
| S0.5 | **現行 apps 版の修正**(§1.7 の glue 側 + 鍵ゼロ化 §1.8) | `a4502b7` の次のコミット | T1〜T5 + 新設 TR(replay)が通る。**差分比較の基準に「正しい動作」を含める** |
| S1 | **カーネル骨格** | `drivers/net/wireguard/` の Kconfig / Make、`wg_noise.c` の再整形、`wg_platform.c`、`wireguard_initialize()` で `wg0` 登録(制御なし)、`sim:wireguard` defconfig | `ifconfig` に `wg0`。`checkpatch.sh -f` クリーン。1〜2 日で切り上げる |
| **S2a** | **最小垂直スライス** | `SIOCSWGIF`(秘密鍵 + up)+ `SIOCSWGPEER`(1 ピア)+ `SIOCGWGIF`/`SIOCGWGPEER`(公開鍵・port・handshake・transfer)。`apps/system/wg` は `set private-key` / `set peer` / `up` / `show` だけ | `verify-sim-wg-runtime.sh` 前半(ランタイム設定でトンネルが通る)が sim で PASS |
| **S4a** | **PROTECTED/KERNEL 証明(前倒し)** | S2a のコードを `qemu-armv7a:knetnsh`(BUILD_KERNEL + virtio-net。`sim` に PROTECTED 構成は無い)で。`wg` は ROMFS 上の ELF | 同スライスがハンドシェイク + ping まで通る。**これが B にした意味** |
| **S2-sp** | **Spresense 到達性スモーク** | 実機 1 枚で `gs2200m &` 後に `SIOCSWGIF` が `wg0` の `d_ioctl` に届くか | ioctl の戻り値がログにある。届かなければ §1.2 の経路を見直してから先へ |
| S2b | **残りの制御** | `setconf` / `saveconf` / `showconf` / `genkey` / `pubkey`、PSK、削除、境界・不正値(TF)、`wg show` 互換 | T1・T3・TF 全 PASS |
| S3 | **移行と実機** | Dockerfile を `WG_IMPL=kernel` に(§2.1)、rcS を `setconf` + `up` に、両ボード T5 | T4 PASS、T5 チェックリスト全項目(13.0.1)、1〜3(master) |
| S4b | knetnsh で T1 相当を全部 | — | T6 |
| S5 | **ドキュメント** | `Documentation/components/drivers/special/net/wireguard.rst`、`Documentation/applications/system/wg/index.rst`(**別 PR**、apache/nuttx 側)、`LICENSE` 追記、[hardware-verification.md](../development/hardware-verification.md) の「トンネル IP は Kconfig 固定」を差し替え | 文書ビルドが通る |
| S6 | **PR 提出**(§4) | — | — |
| S7 | **後続**(別 PR) | `udp_conn` 直結、IPv6、複数インターフェース、稼働中のピア変更(OpenBSD 互換) | それぞれ T1〜T5 |

### 2.1 移行中の二本立て

- **真実は fork ブランチ**: `apache/nuttx` fork の `net-wireguard`、`apache/nuttx-apps` fork の `system-wg`。カーネル側は `net/Kconfig`・`drivers/net/Make.defs`・`drivers/drivers_initialize.c`・`include/nuttx/net/ioctl.h`・`LICENSE`・`Documentation/` の改変を伴うので、ディレクトリのコピーでは再現しない(現行の python 文字列置換パッチをこれに使うと ref ごとに壊れる)
- Dockerfile は `ARG WG_IMPL=apps|kernel` と `NUTTX_REPO`/`APPS_REPO`(+ 既存 `NUTTX_REF`)で切り替え。**一つのイメージには一方だけ**(`CONFIG_NET_WIREGUARD` が衝突する)。タグは `sim-apps-13.0.1` / `sim-kernel-master` のように impl と ref を含める
- `nuttx_port/` はレビュー用の派生物(`scripts/sync-from-fork.sh` で fork から生成)。「提出物とツリーが一致する原則」は fork 側で担保
- 各 `verify-*.sh` はログ 1 行目に impl と ref を出す(`/opt/wg-impl.txt`)。同じスクリプトを 2 つのイメージに回すのが §3.5「同じ結果」の機械的な担保

### 2.2 apps 版を引退させる完了の定義

1. `WG_IMPL=kernel` で T1〜T4 が 13.0.1 と master で PASS(ログを `docs/development/verify-logs/` に保存)
2. T5 が両ボード × 13.0.1 で全項目、両ボード × master で 1〜3
3. T6 が `qemu-armv7a:knetnsh` で PASS
4. **apps 版で `saveconf` したファイルを kernel 版の `setconf` が読める**(`scripts/fixtures/wg0.conf`)
5. タグ `v0.1.0`。同じコミットで apps 版 × 12.7.0 のフルラン(sim 3 本 + QEMU + 両実機)の最終ログを残す
6. `nuttx_port/apps/netutils/wireguard/` の削除と `WG_IMPL` 既定の切り替えを**一つのコミット**で(build-arg を永久に残さない)

---

## 3. テスト戦略

### 3.1 層

| ID | 層 | 何を | 道具 | 頻度 |
|---|---|---|---|---|
| T0 | 静的 | スタイル・SPDX ヘッダ・`.config` に秘密鍵が無いこと | `checkpatch.sh -f`、`nxstyle` | 毎コミット |
| T1 | sim / 機能 + 相互運用 | ランタイム設定でトンネル、Linux カーネル WireGuard と handshake/ping、`down`/`up`、`saveconf`/`setconf` 往復、`wg show` の**スナップショット diff**(`scripts/expected/wg-show.txt`、数値はマスク)、conf fixture | `verify-sim-wg-runtime.sh`(旧 T2 を吸収。Kconfig 鍵が消えるので `verify-sim-wireguard.sh` は削除) | 毎コミット |
| T3 | sim / 複数ピア | 2 ピア同時。鍵をランタイム設定にして**リビルド不要**に | `verify-sim-wg-multipeer.sh` | 毎コミット |
| **TF** | sim / ioctl 単体 | 長さ違いの鍵、all-zero、自己公開鍵、低位点、ピア上限 +1、allowed-ips 上限 +1・重複・不連続 mask、ポート 0、鍵未設定で up、小さすぎるバッファ。全部 `-EINVAL`/`-EEXIST` で `wg show` が変わらない。ピア不要で最速 | `verify-sim-wg-ioctl.sh`(新規) | 毎コミット |
| **TV** | ベクトル(KAT) | X25519(RFC 7748 §5.2 + 低位点)、ChaCha20-Poly1305(RFC 8439 §2.8.2)、XChaCha20-Poly1305(draft-irtf-cfrg-xchacha §A.3.1)、BLAKE2s(RFC 7693 + 鍵付き)、HKDF の中間値、**ハンドシェイク全体**(wireguard-go `noise_test.go` 相当。乱数・時刻を hook して決定論化) | sim 専用 C テスト | 毎コミット。**PR-K1 に含める**(vendored と `crypto/` を同じ入力で突き合わせる) |
| **TR** | replay / なりすまし | scapy で (a) 捕獲 keepalive を別 IP から再送 → endpoint が動かない、(b) 捕獲 initiation 再送 → 応答 1 回、(c) 窓外 counter、(d) 型バイト・長さのファジング | sim + TAP | 毎 PR |
| **TN** | 否定系相互運用 | 間違った peer 公開鍵、PSK 不一致、相手が cookie を要求する状態(Linux 側に負荷)。期待通り**繋がらない/繋がる** | T1 拡張 | 毎 PR |
| T4 | QEMU | NuttX スケジューラ上で T1 相当。Linux 側は `scripts/lib/peer.sh` に共通化 | `verify-qemu-wireguard.sh` | 毎 PR |
| T5 | 実機 | §3.2 | [hardware-verification.md](../development/hardware-verification.md) + `scripts/hil/`(1 接続内でログ収集、判定は人間) | §3.4 |
| T6 | ビルド種別 | `qemu-armv7a:knetnsh` で `wg set private-key` → `set peer` → `up` → Linux から ping 3/3 → `wg show` に handshake。**TZ**: `wg` タスクから `priv->wg` を読んで MPU 例外 | `verify-qemu-wireguard.sh` の knetnsh 版 | 毎 PR |
| **TZ** | ゼロ化 | sim で `wg down` 後に `gcore`、既知の秘密鍵 32 バイトと session key が 0 件 | sim | 毎 PR |
| **TE** | エントロピー | 冷起動 ×3 で `wg genkey` が全部違う、pool の `cryptwarn` が出ない、`.config` が §1.9 の依存を満たす | T5 + T0 | 毎 PR |
| **TT** | 時刻 | (a) 相手が initiator でボード再起動、(b) **ボードが initiator で再起動**、(c) `date -s` で後方へ、(d) RTC 無効(cxd56 late-init 前)で up。全部 30 秒以内に再ハンドシェイク | T5 追加 | 毎 PR(a,b)、リリース前(c,d) |
| T7 | 長時間 | 4 時間ではなく **REJECT_AFTER_TIME × 3(9 分)を跨ぐ rekey を 50 回以上** + endpoint 変更 10 回 + `down/up` 100 回で iob / スタック高水位が単調増加しない(#5) | `scripts/hil/soak.py`(リポジトリに入れる) | PR-K1 提出前 |
| T8 | ビルドマトリクス | `sim:wireguard` defconfig が Linux / Darwin / msys2 で通る(upstream CI)。手元は `scripts/testbuild-subset.list`(sim:nsh, sim:wireguard, qemu-armv7a:netnsh, qemu-armv7a:knetnsh, esp32s3-devkit:wifi, spresense:wifi, spresense:nsh, `CONFIG_NET=n` の 1 つ)+ CMake ビルド | `tools/testbuild.sh` | 毎 PR |

`scripts/verify-all.sh <impl> <ref>` で sim 5 本 + QEMU + knetnsh を順に回し `docs/development/verify-logs/<date>-<impl>-<ref>.log` に落とす。冒頭で `docker system df`、終了時に `docker builder prune --filter until=72h`。GitHub Actions(`.github/` は現在無い)は sim の T0/T1/T3/TF/TV のみ(`--device=/dev/net/tun` と `ip link add type wireguard` が hosted runner で通ることを 1 度確認、`base` を `cache-to: type=gha`)。

### 3.2 実機チェックリスト(T5、両ボード。前提: 一度 `saveconf` 済み)

1. 電源投入のみ(USB 未接続)で `wg0` が上がり telnetd が起動する
2. Windows 公式クライアントとハンドシェイク(`latest handshake` 30 秒以内)
3. トンネル越し ping 0% loss、telnet でコマンド、`webserver &` → HTTP 200
4. `wg show` の transfer が増える。`ps`/`free` を記録(kernel 版はスタック・ヒープが変わる。upstream に問われる)
5. `wg down` → `wg up` で再ハンドシェイク
6. 2 枚同時(10.10.0.0/24 と 10.11.0.0/24)
7. 電源断 → 復帰で 1 に戻る。**Windows 側でなくボードが initiator になる構成でも**(TT-b)

### 3.3 ioctl 固有(TF の内訳)

§1.2 の検証規則を一つずつ否定系で。PROTECTED では「ユーザー空間から鍵を読めない」(`SIOCGWGIF` に秘密鍵が無い、`priv->wg` 直接読みで MPU 例外)。

### 3.4 実機カバレッジの階層化

| 頻度 | 内容 | 所要 |
|---|---|---|
| 毎コミット(CI) | T0、T1、T3、TF、TV(sim、既定 ref) | 10 分 |
| 毎 PR(`verify-all.sh`) | 上 + T4、T6、TR、TN、TZ、testbuild subset、`NUTTX_REF=master` で sim | 30〜40 分 |
| 制御経路・データパスに触ったとき | **Spresense** で T5 1〜5(usrsock 固有の経路を持つ) | 30 分 |
| PR 提出前 | ESP32-S3 × 13.0.1 で T5 1〜5 + TE + TT-a/b | 30 分 |
| リリース相当(PR-K1 提出、レビュー対応後の最終) | 両ボード × {13.0.1, master} で T5 1〜7、TT-c/d、T7 を 1 回 | 半日 + ソーク |
| 凍結時 1 回だけ | apps 版 × 12.7.0 × 全部(`v0.1.0` の証拠) | 半日 |

### 3.5 回帰の基準

移行前の基準は **S0.5 適用後**の apps 版(replay・cookie・ゼロ化を直した状態)。移行後は同じスクリプト・同じ実機手順・同じ `wg show` スナップショットで比較する。

---

## 4. マージ戦略

### 4.1 順序

```
PR-K0  apache/nuttx  sched: RTC_HIRES 回帰(#9)         ── 独立・小。最初。信用を作る
PR-K0' apache/nuttx  net/usrsock: 管理外 ifname の ioctl を -ENOTTY に(#10)  ── 独立
   │
dev@ 投稿(告知)+ apache/nuttx に Issue(設計: drivers/net/、ioctl ABI、crypto/ 利用、乱数・時刻要件)
   │   レビュアーを CC。返信待ちは 1 週間。以後は Draft PR で議論する
   │
PR-K1  apache/nuttx  drivers/net: Add WireGuard virtual network device
   │   コミット 5 分割: (1) include/nuttx/net/wireguard.h + net/Kconfig
   │                  (2) drivers/net/wireguard/(crypto/ 使用、TV テスト込み)
   │                  (3) boards/sim/.../configs/wireguard/defconfig  ← これが無いと CI は 1 行もコンパイルしない
   │                  (4) Documentation/components/drivers/special/net/wireguard.rst
   │                  (5) LICENSE 追記(tun.c のエントリと同書式。NOTICE は触らない)
   │
PR-A1  apache/nuttx-apps  system/wg               ── K1 マージ後(ヘッダ依存)
PR-K1b apache/nuttx  Documentation/applications/system/wg/index.rst  ── A1 と対(nuttx-apps の文書は nuttx 側に別 PR が要る)
   │
PR-K2  udp_conn 直結 / IPv6 / 複数 IF / 稼働中ピア変更(要望があれば)
smartalock/wireguard-lwip へ §1.7 のパッチ(独立)
```

「1 機能 1 PR だからこれ以上割れない」は CONTRIBUTING の読み違え(1.8: 文書は同 PR・別コミット)。PR は 1 本、コミットで割る。

### 4.2 各 PR に添付するもの

- T1/T3/TF/TV/T4/T6 のスクリプト出力、T5 の実機ログ(`wg show`・`ping`・telnet の抜粋。**`.config` は貼らない**)、TZ/TE/TT の結果
- `checkpatch.sh` の結果、`sim:wireguard` の testbuild 結果
- 設計の要約と Issue / dev@ スレッドへのリンク、`rpmsgdrv.c` / `vlan.c` を前例として引く
- コミットは `drivers/net: ...` / `system/wg: ...`、`Signed-off-by` は人間のみ、`Assisted-by:` は実際に使ったモデル名(master で必須化)

### 4.3 想定される論点と答え

| 論点 | 答え |
|---|---|
| なぜ `apps/` でなく `drivers/net/` か | netdev の登録・`devif_poll`・入力はカーネルの仕事。apps に置くと FLAT 限定(実証済み)。前例 `tun.c`/`vlan.c` |
| なぜカーネル部品がソケットを開くのか | `rpmsgdrv.c` と同じ。`udp_conn` 直結は後続 PR |
| BSD コードをカーネルに | `crypto/`・`tun.c` の前例。`ALLOW_BSD_COMPONENTS` ゲート、SPDX + 元の BSD 3 条項全文を保持、`LICENSE` 追記 |
| なぜ `crypto/` を使わないのか | 使う(初回から)。`curve25519()` の戻り値規約と `blake2s()` の引数順の差はマクロで吸収し KAT で検証 |
| DoS 耐性 | cookie 両方向実装、`is_under_load` は直近 1 秒の initiation 数、レート制限バグ修正 |
| 乱数源 | Kconfig で XORSHIFT を禁止、pool のエントロピー不足なら up を拒否 |
| PROTECTED で本当に動くのか | T6(knetnsh)+ TZ のログ |
| `netdev_lowerhalf` を使え | 使う |
| テストは | TV は C テストとして PR に同梱、それ以外はスクリプトと実機ログ |

### 4.4 取り下げ条件

Issue / Draft PR で「`drivers/net/` 以外」「ioctl 以外の機構」となったら S2b の前に設計を直す。**S2a + S4a のスライス結果(動くもの)を添えて問う**ことで「設計の提示」を「動作の提示」にする。

---

## 5. リスクと未決

| リスク | 手当て |
|---|---|
| usrsock 環境で制御経路が届かない | `SIOC?WG*` を `net_ioctl_arglen()` に登録しない(未登録は素通し)。標準 ifreq に依存しない。S2-sp で実機確認。#10 の修正を PR-K0' として並走 |
| 乱数バックエンドが弱いまま握手が成立する | §1.9 の Kconfig 依存。TE |
| ボードが initiator の構成で再起動後に繋がらない | §1.10 の TAI64N 永続化。TT-b |
| Spresense の master/13.x 起動が #9 に依存 | 反映まで Dockerfile パッチ。実機ログに「#9 適用済み」と明記。master に入ったら `pattern not present` を確認して削除 |
| `curve25519()` の戻り値反転・`blake2s()` の引数順 | TV(KAT)を PR-K1 に同梱。vendored と `crypto/` を同じ入力で突き合わせてから vendored を消す |
| `net/Kconfig` / `drivers/net/Make.defs` の変更が他 defconfig を壊す | T8 のサブセット。`NET_WIREGUARD=n` では新コードはコンパイルされないので構文と条件の話 |
| Kconfig 鍵の廃止で未設定ボードが上がらない | T5 の前提を「`saveconf` 済み」に。初回設定用に `scripts/hil/push-conf.py` |
| Windows 側ピア鍵の更新事故 | ボード鍵を `scripts/hil/keys/`(gitignore)に退避し常に同じ鍵を `setconf`。SPIFFS 消去時の手順を [hardware-verification.md](../development/hardware-verification.md) に |
| ESP32-S3 の SPIFFS 形式(12.7.0 ↔ 13.x) | kernel 版は 13.x/master のみなので互換。conf の書式・パスを変えない(fixture) |
| Docker のディスク圧迫(09-17 に 49 GB で VM が落ちた) | `WG_IMPL × ref × target` を全部保持しない。ref は 13.0.1 + master、12.7.0 は凍結後作らない。`verify-all.sh` で prune |
| knetnsh の apps ROMFS / rcS 経路が FLAT と違う | T6 では rcS を使わず fifo から打つ。ヘッドレスは FLAT のみ主張 |
| `wg set` を up 中に許すか | フェーズ 1 は down 時のみ(現行と同じ)。稼働中変更は PR-K2 |
| dev@ が無反応 | 1 週間で Issue + Draft PR。dev@ は告知 |
| 膨らむ 3 点 | (i) `wg` クライアントの書き直し(現行 1459〜2173 行が移動対象) — 固定長 ABI で可変長を捨てる。(ii) PROTECTED は knetnsh 1 構成に限定。(iii) kernel 版の ref は 13.0.1 + master のみ。S7 は PR-K1 のレビュー中に着手しない |

未決(Issue で決める): `drivers/net/wireguard.c` 1 ファイルか `wireguard/` ディレクトリか。`NET_LL_TUN` の扱い。`SIOC?WG*` を将来 arglen に載せるか(rpmsg 転送対応)。

---

## 6. 専門家レビュー(2026-09-18、3 名、v1 → v2 の差分)

3 つの視点で独立にレビューし、食い違いを解いて上に反映した。原文は長いので要点のみ。

### 6.1 NuttX メンテナ / upstream レビュアー視点

計画の「前例」認識が 3 点で事実と食い違い、v1 のままでは最初のレビューで設計ごと差し戻される、という評価。

1. **置き場所**: `net/` 配下で `netdev_register()` を呼ぶコードは無い(`grep` で `netdev_register.c` のみ)。仮想 netdev は全部 `drivers/net/`。最も近い前例は `vlan.c`(専用 SIOC + `d_ioctl` 転送 + lowerhalf)と `rpmsgdrv.c`(kthread + `psock_*`) → **§1.1 を `drivers/net/` に変更**
2. **暗号**: 4 プリミティブは `crypto/` に全部ある(XChaCha20 も `chachapoly.c:288`)。vendored + `-D` リネームの二重持ちは却下される → **§1.4 を初回 PR から `crypto/` に変更**
3. **無改変**: checkpatch/nxstyle に除外リストは無く CI は差分全体にかける。`tun.c` と同じく BSD ヘッダ + SPDX を残して再整形 → **§1.1 / §1.7**

ほか: ioctl はフラット固定長構造体(`vlan_ioctl_args` 型)、`netdev_lowerhalf` を使う、`sim:wireguard` defconfig が無いと CI がコンパイルしない、`wg` の文書は `apache/nuttx` 側に別 PR、PR は 1 本でコミット 5 分割、`d_ifname` は `"wg%d"`、dev@ は告知で議論は Issue + Draft PR、`Assisted-by:` は master で必須。

### 6.2 WireGuard プロトコル / セキュリティ視点

計画が置き場所・ABI・ビルド種別に寄っていて、カーネルに入れた瞬間に見られる 4 点(乱数・時刻・cookie/DoS・鍵ライフサイクル)が「現行と同じ」で済まされている、という評価。**現行 glue のプロトコル逸脱**を深刻度順に:

1. **[Critical]** replay 検査より前に endpoint を更新し、keepalive は検査なし → 認証済みパケット 1 個の再送で endpoint 乗っ取り(`nuttx-wireguardif.c:645-703`)→ §1.7
2. **[Critical]** `arc4random_buf()` は `/dev/urandom` が無いと `clock()` に黙ってフォールバック。cxd56 の既定は XORSHIFT。sim/QEMU の Dockerfile も XORSHIFT → §1.9
3. **[High]** cookie reply を捨てている(負荷中の Linux/Go サーバに永久に繋がらない)、vendored のレート制限が減算逆で無効、`is_under_load` 固定 false → §1.7
4. **[High]** 鍵のゼロ化漏れ(`wg_initialize()` のスタック鍵、`g_staged`、`wg_down()`、`wg_ifdown()`)、`showconf` が stdout に秘密鍵、`pubkey` が引数で秘密鍵 → §1.8
5. **[High]** TAI64N の単調性(ボードが initiator の構成では再起動で繋がらない)→ §1.10
6. **[High]** `SIOCSWG` は `netdev_ifr_ioctl()` に case 追加が要る。`net_ioctl_arglen()` に載せると usrsock に横取りされる → §1.2
7. **[Medium]** ABI をフラットに、検証規則(自己公開鍵・低位点・allowed-ips 重複・`-ELOOP`)、PSK を ABI に、`curve25519()` の戻り値反転と `blake2s()` の引数順、KAT テスト(TV)を PR-K1 に、ロック規約、down の順序
8. 追加テスト TV / TN / TR / TF / TZ / TE / TT と T7 の rekey 回数ベース化 → §3

### 6.3 開発リード / 検証視点

方向は正しいが「動いている実機デモを壊さずに移行するメカニズム」が無い、という評価。

1. **S2 を最小垂直スライス(鍵 + ピア 1 + up + show)と残りに分割し、PROTECTED 証明を実機より先に**(`sim` に PROTECTED 構成は無い → `qemu-armv7a:knetnsh`)→ §2
2. **fork ブランチを真実に、Dockerfile は `WG_IMPL=apps|kernel`**、`nuttx_port/` は生成物。カーネル側は 5〜6 ファイルの改変を伴いコピーでは再現しない → §2.1
3. apps 版引退の完了の定義 6 項目、タグ `v0.1.0` → §2.2
4. テスト: T2 を T1 に統合、T3 をリビルド不要に、ioctl 単体(TF)、`wg show` スナップショット diff、conf fixture、testbuild subset、`verify-all.sh`、GitHub Actions → §3.1
5. 実機カバレッジの階層化(毎 PR は ESP32-S3、Spresense は制御経路を触ったとき)→ §3.4
6. リスク台帳 10 項目(#9 依存、Windows 側鍵運用、Docker ディスク、rcS の変更、`saveconf` と秘密鍵)→ §5

### 6.4 食い違いと解決

| 論点 | 意見 | 解決 |
|---|---|---|
| ioctl が usrsock に横取りされるか | 検証: 横取りされる(`/dev/wg0` か #10 修正の前提化)。メンテナ: 未登録 SIOC は `-ENOTTY` で素通し。セキュリティ: `arglen` に載せた瞬間に転送される | **独自 SIOC を `netdev_ifr_ioctl()` に追加し `arglen` には載せない。標準 ifreq(`SIOCSIFFLAGS`/`SIOCSIFADDR`)に依存しない**。S2-sp で実機確認。#10 修正は並走(PR-K0') |
| `SIOCGWG` が秘密鍵を返すか | 検証: `saveconf` に読み戻しが要る。メンテナ: 返さないのは良い。セキュリティ: 返さない(B 案の存在意義)、`saveconf` はファイル正本化 | **既定で返さない。設定ファイルを秘密鍵の正本にし、`wg set private-key` はファイル + カーネルの両方に書く。デバッグ用 Kconfig で読み戻し可** |
| `pubkey` の計算場所 | v1: ioctl で一本化。検証・メンテナ・セキュリティとも apps 側 | **apps 側に小さい X25519** |
| ピアの指定 | メンテナ: index。セキュリティ: index は削除でずれる、公開鍵キー | **設定・削除は公開鍵キー、列挙(`show`)のみ index** |
| PROTECTED の vehicle | v1: sim。検証・メンテナ: sim に無い | **`qemu-armv7a:knetnsh`** |
| 暗号の切り替え時期 | v1: フェーズ 2。メンテナ: 初回から。セキュリティ: KAT が先 | **初回 PR から `crypto/`、KAT(TV)を同梱** |

### 6.5 v1 から変わらなかったこと

B(カーネル側)という結論、`wg` を薄いクライアントにする分業、鍵を Kconfig に焼かない、12.7.0 を切る、実機 2 枚 × sim × QEMU を回帰基準にする、#9 を最初に出す。
