# Phase 4 開発ログ: 実機ビルド・書き込み・実ネットワーク検証

## 目標

実際に手元にある実機(ESP32-WROOM-32、ESP32-S3、Sony Spresense メインボード)で `CONFIG_NET_WIREGUARD=y` のビルドを通し、書き込み・起動・実ネットワーク越しの WireGuard 通信まで確認する。

**結論を先に:** **ESP32-S3 で完全成功。** 実機を Wi-Fi 経由で実際のアクセスポイントに接続し、Windows 上の公式 WireGuard クライアントと実際にハンドシェイクを成立させ、トンネル越しの ping(0% packet loss)まで確認できた。さらに telnetd をトンネル越しに使おうとした際に見つかった「TCP のアプリケーションデータだけがトンネルを通らない」バグ(原因: LPWORK ワーカースレッドから `sendto()` する際にファイルディスクリプタがそのスレッドのタスクグループに属さず `EBADF` になっていた)を特定・修正し、トンネル越し telnet セッションでのコマンド実行まで実機で確認した(詳細は後述の節)。**Spresense (ARM Cortex-M4F) は、iS110B Wi-Fi Add-on を載せて実 Wi-Fi 経由のハンドシェイク・トンネル越し ping まで確認した**(2026-09-16)。その過程で usrsock 環境で `wg0` の ioctl が横取りされてハンドシェイクが始まらないバグを見つけて修正した(詳細後述)。当初「ハードウェア故障の疑い」としていたのは CP210x ドライバ未インストールが原因の誤診断で、後日訂正した。ESP32-WROOM-32 のみ、GPIO0 経路の故障により書き込みに到達できていない。

---

## 環境準備

### CP210x ドライバ

ESP32-WROOM-32 ボードの USB シリアルチップ (Silicon Labs CP2102、VID_10C4/PID_EA60) が Windows に `Code 28: ドライバがインストールされていません` として認識され、COM ポートが割り当てられていなかった。Silicon Labs 公式サイトから CP210x Windows Driver をインストールして解決(`COM5` として認識)。

### esptool / arduino-cli (Windows ホスト側)

Docker Desktop (Windows) は USB シリアルデバイスに直接アクセスできないため、書き込みは Windows ホスト側で直接行う方式にした:

- `pip install esptool`(Windows の Python 3.13 に直接インストール)
- 後述のクロスチェック用に `arduino-cli`(winget 経由)+ ESP32 core も導入

---

## ESP32-S3 — 実機での完全成功

翌日、ESP32-S3 搭載ボード(Freenove 製、16MB フラッシュ、PSRAM 8MB 内蔵、`ESP32-S3 (QFN56) revision v0.2`)が到着し、そのまま実機検証を完了させた。

### チップ判定・接続 — ボタン操作不要で一発成功

`esptool chip_id` を実行したところ、**特別なボタン操作(BOOT/EN)なしで、デフォルトの自動リセットのみで一発接続に成功した**。無印 ESP32-WROOM-32 で何十回も失敗した `Wrong boot mode detected` は一度も発生しなかった。実際にはこのボードも USB シリアルブリッジチップ(WCH 製 CH343、VID_1A86/PID_55D3)経由での接続だったが、ドライバも Windows に最初から入っており(手動インストール不要)、接続も安定していた。無印 ESP32 で踏んだ問題は、チップ世代の違いというよりは**個体・ボード側の自動リセット回路の問題だった可能性が高い**ことがここで裏付けられた。

### ビルド・書き込み

`Dockerfile` に追加していた `esp32s3` ステージ(前日に準備済み、`esp32s3-devkit:wifi` ボード + `xtensa-esp32s3-elf` ツールチェイン)でビルドし、そのまま書き込み。

書き込みオフセットは無印 ESP32(`0x1000`)と異なり **`0x0000`**(コンテナ内で `make flash ESPTOOL_PORT=<fake> ESPTOOL_BINDIR=./` を実行して実際のコマンドを確認した):

```bash
python -m esptool -c esp32s3 -p COM7 -b 921600 write_flash -fs detect -fm dio -ff 40m 0x0000 nuttx.bin
```

一発で `Hash of data verified` が出て成功。シリアルコンソール(pyserial 経由、115200 baud)で `nsh>` プロンプトへの起動を確認。

### `wg0` 起動確認(テスト鍵)

テスト用の秘密鍵を `kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PRIVATE_KEY` で設定してビルドし直し、再書き込み:

```
nsh> wg
wg0 is up (listen port 51820)
nsh> ifconfig
wg0	Link encap:TUN at UP mtu 1500
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0
```

**このプロジェクトで初めて、実機シリコン上で `wg0` の起動を確認できた。**

### 実 Wi-Fi + 実ピアでのハンドシェイク

実際の家庭用 Wi-Fi(TP-Link ルーター)の SSID・パスフレーズを `CONFIG_NETINIT_WAPI_SSID`/`PASSPHRASE` に設定し、`CONFIG_NETINIT_DHCPC=y` を有効化。ピアには **Windows 上の公式 WireGuard クライアント**(`winget install WireGuard.WireGuard`)を使用した。Linux カーネル実装ではなく Windows 版公式クライアントとの相互運用性を確認する意味もあった。

- Windows 側の鍵ペアは Docker 上の `wireguard-tools`(`wg genkey` / `wg pubkey`)で生成
- Windows 側の `.conf` を作成し、WireGuard アプリの GUI で「ファイルからトンネルをインポート」→「アクティブ化」(`wireguard.exe /installtunnelservice` はサービスインストールに管理者権限が必要で、非対話的には実行できなかったため GUI 操作に切り替えた)
- ESP32-S3 側は `CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP` に Windows 機の LAN IP(`192.168.0.216`)を設定し、ESP32-S3 側からハンドシェイクを開始する構成にした(Windows 側はエンドポイントを指定せず、着信パケットから自動学習させた)

再ビルド・再書き込み後、`wlan0` が実際のルーターから **DHCP で本物の LAN アドレス(`192.168.0.152`)を取得**していることを確認。Windows 側でトンネルをアクティブ化した直後、ESP32-S3 側の `wg show` で:

```
peer: 5J5rgkz5RB0CB1hIZae5V3jQjisRjqOrry7Scca9YjE=
  endpoint: 192.168.0.216:51820
  latest handshake: 11 seconds ago
  transfer: 336 B received, 240 B sent
```

**実際のハンドシェイクが成立。** Windows 側から `ping 10.10.0.2`(ESP32-S3 の WireGuard トンネルアドレス)を実行したところ:

```
Pinging 10.10.0.2 with 32 bytes of data:
Reply from 10.10.0.2: bytes=32 time=11ms TTL=128
Reply from 10.10.0.2: bytes=32 time=8ms TTL=128
Reply from 10.10.0.2: bytes=32 time=11ms TTL=128
Reply from 10.10.0.2: bytes=32 time=8ms TTL=128

Ping statistics for 10.10.0.2:
    Packets: Sent = 4, Received = 4, Lost = 0 (0% loss),
```

**4/4 パケット、0% packet loss。** sim・QEMU の仮想ネットワーク環境で確認済みだった WireGuard の実装が、本物の実機・本物の Wi-Fi・本物の非 Linux ピア(Windows 公式クライアント)との組み合わせでも正しく相互運用できることを実証した。

### Flash / RAM 使用量

```
nsh> free
                 total       used       free    maxused    maxfree  nused  nfree
      Umem:     291696      76072     215624      81448     210704    150      6
```

```
$ xtensa-esp32s3-elf-size nuttx
   text	   data	    bss	    dec	    hex	filename
 660310	  11868	 256060	 928238	  e29ee	nuttx
```

ヒープ(291,696 バイト)のうち使用 76,072 バイト・空き 215,624 バイトと十分な余裕がある。書き込みイメージ(`nuttx.bin`)は約 675KB(16MB フラッシュに対して十分小さい)。

---

## ESP32-WROOM-32

### チップ判定

`esptool chip_id` で自動判定: **ESP32-D0WDQ6 (revision v1.0)**、Wi-Fi/BT、デュアルコア。NuttX のボードコンフィグは `esp32-devkitc`(WROOM/WROVER 系の DevKitC 系ボードに対応)を使用。

### Docker ビルド (Xtensa トレイン)

`Dockerfile` に `esp32` ステージを追加。既存の `base` ステージ(NuttX/apps/wireguard コンポーネントのクローン・配置まで共通)の上に:

1. Xtensa ESP32 用ツールチェイン(NuttX 公式 CI が参照している Espressif prebuilt: `xtensa-esp32-elf-12.2.0_20230208-x86_64-linux-gnu.tar.xz`)を展開
2. `esptool`(Python)をインストール — NuttX 自身のビルド末尾 `MKIMAGE: ESP32 binary` ステップ(ELF → `nuttx.bin` 変換)に必要
3. `./tools/configure.sh esp32-devkitc:wifinsh` + WireGuard 用の Kconfig(`ALLOW_BSD_COMPONENTS`・`NET_TUN`・`NET_TUN_PKTSIZE=1500`・`NET_SOCKOPTS`・`NET_WIREGUARD`・`DEV_URANDOM` + `DEV_URANDOM_ARCH`)

**ビルドは一発で成功した。** `nuttx-platform.c`・`nuttx-wireguardif.c`・`wg_main.c` に一切のコード変更は不要だった(sim/qemu 向けに書いたコードがそのまま ESP32 実機ターゲットでもコンパイル・リンクできた)。

### 書き込み試行

`make flash` が実際に使うコマンドをコンテナ内で確認:

```
esptool -c esp32 -p <port> -b 921600 write_flash -fs detect -fm dio -ff 40m 0x1000 nuttx.bin
```

`nuttx.bin` を `docker cp` で Windows ホストに取り出し、`python -m esptool` で書き込みを試みたが、**毎回同じエラーで失敗**:

```
A fatal error occurred: Failed to connect to ESP32: Wrong boot mode detected (0x13)!
The chip needs to be in download mode.
```

以下の方法をすべて試したが、結果は変わらなかった:

- BOOT を押しながら EN を押して離す(複数のタイミングパターン)
- BOOT を押したまま USB を抜き差し(コールドブート時に GPIO0 を Low にする方法)
- BOOT を押しっぱなしにしたまま esptool を実行(離すタイミングの問題を排除)
- コマンドを先に実行してから(接続待ちの間に)ボタン操作(チャット越しのタイムラグを排除)

**クロスチェック:** `arduino-cli`(独立した別実装、デフォルトの自動リセットのみでボタン操作なし)でも `esp32:esp32:esp32` FQBN で Blink スケッチのコンパイル・書き込みを試したが、**全く同じ `Wrong boot mode detected (0x13)` で失敗**。

2つの独立したツール・複数の操作方法すべてで同一の症状が出たことから、esptool の呼び出し方の問題ではなく、**ボード側のハードウェア(自動リセット回路または BOOT ボタン)の問題である可能性が高い**と判断した。

### 追加調査 (2026-08-29): 故障箇所を GPIO0 経路に特定

USB ケーブルを別のものに交換して再試行したが、**症状は同一**(`Wrong boot mode detected (0x13)`)。ケーブルは原因ではなかった。

ここで、このエラーメッセージ自体が「esptool がチップと通信できていて、ブートモードを読めている」ことを意味している点に着目し、pyserial で DTR/RTS を直接叩いて自動リセット回路の左右どちらが壊れているかを切り分けた。ESP32 の DevKitC 系は DTR→GPIO0、RTS→EN をそれぞれ反転トランジスタ経由で駆動している。

| テスト | 操作 | 結果 |
|---|---|---|
| A | RTS のみトグル (GPIO0 は High のまま) | **チップが再起動し ROM バナーが出る** → EN 経路は正常 |
| B | esptool と同じ標準シーケンス (DTR で GPIO0 を Low) | `boot:0x13` = 通常起動のまま |
| C | DTR の極性を反転 (反転トランジスタが無いボードを想定) | `boot:0x13` |
| D | DTR と RTS の役割を入れ替え | `boot:0x13` |
| E | ホールド時間を 0.6 秒に延長 (EN 側の大容量コンデンサ対策) | `boot:0x13` |

**結論:**

- シリアル通信・チップ本体・**EN(リセット)経路は正常**。テスト A で実際にリセットがかかり ROM バナーが読めている
- **GPIO0 を Low に落とす経路だけが機能しない。** 極性・線の割り当て・タイミングのどれを変えても一度もダウンロードモードに入らない
- BOOT ボタンも以前の試行で全て失敗している。**GPIO0 を Low にする2つの独立した経路(ボタンとトランジスタ)が両方とも効かない**ことになり、これは「GPIO0 が High に固着している」という単一の原因で説明がつく

これにより、症状は「ソフトウェアでは回避不能なハードウェア故障」であることが確定した。残る切り分けはボタンとトランジスタの両方をバイパスする GPIO0–GND のジャンパ直結のみで、それでもダウンロードモードに入らなければ GPIO0 の短絡または端子損傷ということになる。

なお、**ビルド自体は成功している**ため「WireGuard コンポーネントが無印 ESP32 向けにコード変更なしでビルドできる」という移植性の主張には影響しない。実機での書き込み確認には別個体が必要。

---

## Sony Spresense (メインボード単体)

### Docker ビルド (Cortex-M4F)

Spresense (CXD5602, ARM Cortex-M4F) は既存の `arm-none-eabi-gcc`(qemu-armv7a 用に導入済み)がそのまま使えた。新規ツールチェインの追加は不要。

`Dockerfile` に `spresense` ステージを追加。`./tools/configure.sh spresense:nsh` + WireGuard 用 Kconfig を設定したところ、2つの見落としがあった:

1. `spresense:nsh` はデフォルトで **`CONFIG_NET` 自体が無効**(最小構成の NSH のみ)。`CONFIG_NET`・`CONFIG_NET_IPv4`・`CONFIG_NET_UDP`・`CONFIG_NET_SOCKOPTS` を明示的に有効化する必要があった。
2. `CONFIG_SCHED_WORKQUEUE` も無効で、`drivers/net/tun.c` のビルドが `#error Work queue support is required` で失敗した(sim/qemu ではデフォルトで有効だったため Phase 2/3 では気づかなかった依存関係)。`CONFIG_SCHED_WORKQUEUE`・`CONFIG_SCHED_HPWORK`・`CONFIG_SCHED_LPWORK` を追加して解決。

これらを追加した後、**ビルドは成功**し、NuttX の標準ビルドフロー内で `tools/cxd56/mkspk` が自動的に呼ばれて `nuttx.spk`(書き込み用パッケージ形式)が生成された。ここでもコンポーネント自体のコード変更は不要だった。

### 書き込みツールの入手

Spresense の書き込みには NuttX リポジトリに同梱されていない Sony 独自ツールが必要。`sonydevworld/spresense` リポジトリの `sdk/tools/windows/` から Windows 向けの実行ファイルを直接取得した:

- `flash_writer.exe` — X-Modem 経由で `.spk` を書き込むツール
- `xmodem_writer.exe`
- `cxd5602cdc-usb-driver.zip` — USB CDC ドライバ(未インストールのまま作業を止めた)

### 書き込み試行 — USB デバイスとして認識されない

`nuttx.spk` を Windows ホストに取り出し、Spresense ボード(メインボード単体、拡張ボードなし)を接続したが、**Windows 上で一切 USB デバイスとして列挙されなかった**(`Get-PnpDevice`/`Win32_SerialPort` のどちらにも新しいデバイスが一切出現しない。「不明なデバイス」としてすら出ない)。

切り分けのため以下を試したが、状況は変わらなかった:

- USB ケーブルの交換(ただし同一ケーブルで ESP32 は正常にデータ通信できていたため、単純な「充電専用ケーブル」説は弱い)
- PC 側の USB ポートの変更

電源 LED(緑・青)は点灯しており電源自体は供給されているが、USB データ通信の列挙が一切発生しない状態。CDC ドライバのインストールは、そもそも列挙されない状態では試す意味がないため保留した。原因はハードウェア側(USB コネクタの半田不良、ボード自体の初期不良など)の可能性が高いが、未特定。

### 訂正 (2026-08-29): ハードウェア故障ではなかった — 実機で NuttX 起動・`wg0` 起動を確認

**上記の「ハードウェア故障の疑い」は誤りだった。** 後日再検証したところ、Spresense は正常に動作しており、原因は **CP210x ドライバが未インストールだったこと**と判明した。

Spresense メインボードの USB シリアルは **Silicon Labs CP210x ブリッジ**であり、無印 ESP32 DevKitC と同じチップを使っている。当時は CP210x ドライバが入っておらず、そのため列挙されなかった。その後 ESP32 のために同じドライバをインストールしたことで、Spresense も認識されるようになっていた。

**当時の切り分けが誤った理由:**

- `Get-PnpDevice` は既定で「過去に接続した非存在デバイス」も返すため、接続前後で件数を比較しても差分が出ず、「変化なし」に見えていた。`-PresentOnly` を付けて現在接続中のものだけを見る必要があった
- CP210x が既に別デバイス(ESP32)で見えていたため、同じ `VID_10C4&PID_EA60` を ESP32 のものと思い込んでいた。**シリアル番号部分が異なる**(ESP32: `\0001`、Spresense: `\C20C66A8...`)ことに気づけば、別個体だと分かった

**検証結果:**

Sony 提供の `flash_writer.exe`(`sonydevworld/spresense` の `sdk/tools/windows/`)で書き込みに成功した:

```
$ ./flash_writer.exe -s -c COM6 -d -b 921600 nuttx.spk
>>> Install files ...
install -b 921600
Install nuttx.spk
|0%-----------------------------50%------------------------------100%|
######################################################################
132288 bytes loaded.
Package validation is OK.
Saving package to "nuttx"
updater# Restarting the board ...
```

起動後、NSH が立ち上がり `wg` builtin が使えることを確認:

```
NuttShell (NSH) NuttX-12.7.0

nsh> wg set private-key <秘密鍵 44 文字。ここには実物を貼らないこと>
nsh> wg up
wg0 is up (listen port 51820)

nsh> wg show
interface: wg0
  public key: iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=
  listening port: 51820

nsh> ifconfig
wg0	Link encap:TUN at UP mtu 1500
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0

nsh> wg pubkey <秘密鍵 44 文字。ここには実物を貼らないこと>
iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=

nsh> wg down
wg0 is down
```

確認できたこと:

- **ARM Cortex-M4F 実機で `wg0` の netdev 登録・起動が成立**(Xtensa の ESP32-S3 に続く2つ目のアーキテクチャ)
- **`wg genkey` / `wg pubkey` / `wg set` / `wg up` / `wg down` がすべて動作。** `wg pubkey` の出力が `wg show` の interface public key と完全一致しており、暗号導出が ARM 上でも正しい
- **実行時設定の価値が実証された。** Spresense ステージは Kconfig に秘密鍵を設定していないため、従来なら鍵を埋めて再ビルド・再書き込みが必要だった。実行時設定により**リビルドなしで鍵を投入して `wg0` を起動**できた
- `wg down` 後に `ifconfig` から `wg0` が消え、`ps` にも `wg_rx` が残らないことを確認 — teardown が別アーキテクチャでも正しい

~~**未確認:** Spresense メインボードには Wi-Fi が内蔵されていないため、実ピアとのハンドシェイク・トンネル疎通は依然として検証できない(別売りの GS2200M 拡張モジュールが必要)。~~ → 下の 2026-09-16 の節で解決。

なお起動時に `cxd56_farapiinitialize: Mismatched version: loader(20585) != Self(20591)` の警告が出るが、NSH の動作および `wg` の動作には影響していない。Sony 提供のローダ/GNSS ファームを更新すれば消えるはず。

### 追記 (2026-09-16): iS110B Wi-Fi Add-on で実 Wi-Fi 疎通 — usrsock 環境のバグを発見・修正

Wi-Fi Add-on ボード iS110B(GS2200M、v1.0C)が届いたので、ESP32-S3 と同じ AP・同じ Windows 公式クライアントを相手に疎通を試みた。**結果: ハンドシェイク成立、トンネル越し ping 4/4(RTT 141〜143 ms)。** 手順は [hardware-verification.md](hardware-verification.md) の Spresense 節にまとめた。ここには、たどり着くまでに踏んだ問題を時系列で残す。

**ステージ追加:** `Dockerfile` に `spresense-wifi` ステージを追加(`spresense:wifi` ベース)。GS2200M は ESP32 の `wlan0` と違い、`CONFIG_NET_USRSOCK` 経由でソケット API をユーザ空間デーモン(`gs2200m` builtin)にプロキシする方式。`net/usrsock` は netdev を登録しないので、`ifconfig` に出る `wlan0` はドライバが自前で見せているもの。

**1. 起動直後の ASSERT、応答が全バイト 0xFF(半日かかった)**

`CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C` を足しても、SPI クロックを 10→4 MHz に落としても、upstream の PR #2707(`_read_data_len()` の遅延位置の修正、12.7.0 に未反映)を当てても、**スタックダンプが1バイトも変わらない**。`CONFIG_NDEBUG=y` のため `ASSERT()` に file/line が付かず、生スタックダンプを `addr2line` にかけて追った関数は残骸で(call trace ではない)、誤った方向に時間を使った。ドライバに `wlerr()` を挿して bisect しようとしたら今度は何も出ない — `wlerr` は `CONFIG_DEBUG_WIRELESS_ERROR` 無しでは no-op に展開される。有効にしてようやく見えたのが:

```
_read_data_len: gs2200m res: ff ff ff ff ff ff ff ff (n=0)
```

MISO が浮いている signature。Sony 系の Arduino ライブラリ(jittermaster/GS2200-WiFi の TypeC 対応版、`Init_GS2200_SPI_type(iS110B_TypeC)`)を `arduino-cli` で焼いても `GPIO37=1` / `res: ff ff ...` で完全に同じ → ソフトウェアではないと確定。iS110B のコネクタピンに曲がりがあり、直して挿し直したら:

```
_read_data_len: gs2200m res: a5 12 00 00 00 13 00 da (n=0)   ← 0x12 = RD_RESP_OK
_parse_pkt_in_s1: +++++ (msize=16, msg=Serial2WiFi APP|)
NuttShell (NSH) NuttX-12.7.0
```

教訓: `res` の生バイトを最初に見ていれば1時間で終わっていた。デバッグ print を挿す前に、そのマクロが有効かを `.config` で確認すること。

**2. 関連付けは成功するのに疎通しない**

`AT+WA` は通って `10.0.0.2:255.255.255.0:10.0.0.1` と出るが、実ネットワークは `192.168.0.0/24`。`CONFIG_NETINIT_IPADDR` を変えても効かない — `gs2200m_ioctl_assoc_sta()` が `CONFIG_WL_GS2200M_DISABLE_DHCPC` 有効時に `"10.0.0.2"` を**固定文字列で** `AT+NSET` に投入している。`spresense:wifi` の既定がこれを有効にしているので、無効化して内蔵 DHCP を使う(`AT+NDHCP=1` → `192.168.0.115` 取得)。

**3. `gs2200m` を実行すると NSH が戻ってこない**

`gs2200m_main()` は `gs2200m_loop()` で usrsock デーモンとして回り続ける設計。`gs2200m <ssid> <pass> &` で起動する。

**4. pyserial の接続オープンでボードがリセットされる**

CP210x の DTR 自動リセット。検証スクリプトで接続を開き直すたびに Wi-Fi 接続も `wg` の staged config も消えていて、「`wg set private-key` したのに `no private key configured`」「さっき `wg up` したのに `wg0 is not up`」と見えた。一連の操作は1接続内で行う。

**5. `wg up` は成功、`wg show` は `transfer: 0 B sent` のまま(本命)**

`persistent-keepalive 5` にして 30 秒待っても 1 バイトも出ない。`wg_run_timers()` を読むと、ハンドシェイク開始は `peer->active` が前提で、これは `wg_ifup()`(`d_ifup` callback)でしか立たない。`wg_ifup()` は `wg_configure_address()` の `netlib_ifup("wg0")` → `SIOCSIFFLAGS` ioctl → `netdev_ifr_ioctl()` → `netdev_ifup()` 経由で呼ばれるはずだった。

`wg up` 時のログにその答えがあった:

```
gs2200m_ioctl_ifreq: +++ start: cmd=702       ← SIOCSIFADDR
gs2200m_send_cmd: +++ cmd=AT+NSET=10.10.0.2,255.255.255.0,192.168.0.1
gs2200m_ioctl_ifreq: +++ start: cmd=71a       ← SIOCSIFFLAGS
gs2200m_ioctl_ifreq: +++ end:
```

`wg0` 宛ての ioctl を **GS2200M ドライバが処理している**。`netlib_*()` は使い捨ての `AF_INET` ソケットに ioctl を投げる実装で、`CONFIG_NET_USRSOCK` 環境ではそのソケットが usrsock のもの。`net/netdev/netdev_ioctl.c` の `netdev_ioctl()` は

```c
if (psock->s_sockif && psock->s_sockif->si_ioctl)
    ret = psock->s_sockif->si_ioctl(psock, cmd, arg);   /* usrsock → デーモン */
if (ret != OK && ret != -ENOTTY)
    return ret;
```

と usrsock を先に呼び、`OK` か `-ENOTTY` 以外ならそこで打ち切る。`"wg0"` を名前で探す `netdev_ifr_ioctl()` は後段なので届かない。`drivers/wireless/gs2200m.c` の `gs2200m_ioctl_ifreq()` は `ifr_name` を一切見ず:

- `SIOCSIFADDR` / `SIOCSIFNETMASK` → 自分の `d_ipaddr` を上書きして `AT+NSET` → **Wi-Fi 側の IP が `10.10.0.2` に化ける**
- `SIOCSIFFLAGS` → `switch` に無く `default: -EINVAL` → `netlib_ifup()` は失敗(戻り値は見ていなかった)→ `wg_ifup()` は永遠に呼ばれない

修正は `nuttx-wireguardif.c` の `wg_configure_address()` / `wg_down()`。自前の `struct net_driver_s` なのだから ioctl を経由する必要はなく、`net_lock()` 下で `d_ipaddr` / `d_netmask` を直接書いて `netdev_ifup()` / `netdev_ifdown()` を呼ぶ。これは `netdev_ifr_ioctl()` が内部でやっていることそのものなので、ESP32-S3 のような通常 netdev 環境でも動作は変わらない。`netutils/netlib.h` への依存も無くなった。修正後:

```
wlan0	inet addr:192.168.0.115 ...        ← 書き換えられなくなった
wg0	Link encap:TUN at RUNNING mtu 1420  ← carrier on
  latest handshake: 16 seconds ago
```

```
> ping 10.10.0.2
Packets: Sent = 4, Received = 4, Lost = 0 (0% loss)
```

このバグは「usrsock 方式の Wi-Fi ドライバ + 別の netdev を自前登録するアプリ」という組み合わせで初めて顕在化する。GS2200M ドライバ側が `ifr_name` を見て自分宛てでなければ `-ENOTTY` を返すのが筋で、upstream に報告する価値がある。

**副産物:** 起動直後の最初の `wg genkey` が、再起動をまたいで毎回 `KCsDACzp0RA26xvaHPsY2jjxW8P2+FxcEOobWTQ6xkQ=` を返す(同一セッション内の 2 回目以降は変わる)。`drivers/crypto/dev_urandom.c` の `devurandom_register()` が xorshift128 を定数(w=97, x=101)でシードしているのが原因。同日中に `CONFIG_CRYPTO_RANDOM_POOL` + `CONFIG_DEV_URANDOM_RANDOM_POOL` に切り替えて解決した(下記)。

### 追記 (2026-09-16 夜): トンネル越し telnet / HTTP のデモを Spresense でも成立させる

ESP32-S3 のデモ動画(telnet → コマンド → `webserver &` → ブラウザ)を Spresense でも再現するのが目標。結果: **telnet でのコマンド実行、`webserver &` からデモページの HTTP 200 取得まで成功。** 無負荷 RTT 8〜9 ms。ここでも 4 つ詰まった。

**1. `CONFIG_CRYPTO_RANDOM_POOL` を足したら `wg up` がハードフォールト**

`PC: 632e3462`(ASCII "b4.c")、`LR` は `wireguard_init()` の BLAKE2s 呼び出し。`arm-none-eabi-nm` で `libcrypto.a` と wireguard のオブジェクトを突き合わせると、`blake2s` / `blake2s_init` / `blake2s_update` / `blake2s_final` / `chacha20poly1305_encrypt` / `chacha20poly1305_decrypt` / `xchacha20poly1305_encrypt` / `xchacha20poly1305_decrypt` / `poly1305_update` / `poly1305_finish` の 10 個が両方で定義されていた。NuttX 本体の `crypto/blake2s.c` は `blake2s_state` を取り、vendored 側は `blake2s_ctx` を取る。先にリンクされた方が勝ち、もう一方の呼び出し側のスタックが壊れる。

`Makefile` / `CMakeLists.txt` で vendored 側がエクスポートする全 18 シンボル(上の 10 個 + `chacha20` / `chacha20_init` / `hchacha20` / `poly1305_init` / `x25519` / `X25519_BASE_POINT` / `crypto_equal` / `crypto_zero`)を `-D<name>=wg_<name>` でリネーム。プリプロセッサで一貫して置換されるので vendored ファイルは byte-identical のまま。`CONFIG_CRYPTO=y` は普通の設定なので、これは upstream に出す前に必ず踏まれていた。

**2. カーネル側に TCP/UDP スタックが無い**

`denyinet on`(後述)の後で `telnetd &` が `psock_socket: socket address family unsupported: 2` で死ぬ。`spresense:wifi` は `CONFIG_NET_TCP_NO_STACK=y` / `CONFIG_NET_UDP_NO_STACK=y` の usrsock 専用構成で、カーネルには ICMP しか無い(だから ping だけは通っていた)。両方外す。

**3. `denyinet` — トンネル越し TCP を成立させるための小さなヘルパー**

`CONFIG_NET_USRSOCK` では AF_INET の `socket()` が全部 GS2200M デーモンに行き、Wi-Fi モジュール内の TCP/IP で処理される。`wg0` で復号したパケットはカーネル側の IP スタックに入るので、telnetd / webserver のリスナーは**カーネル側**に居なければならない。usrsock には `SIOCDENYINETSOCK`(`DENY_INET_SOCK_ENABLE`)という ioctl があり、以後の AF_INET `socket()` をデーモンが `-ENOTSUP` で拒否してカーネルにフォールバックさせられる(LTE の alt1250 デーモンが使っている機構)。NSH から打てるコマンドが無いので `docker/spresense-denyinet/` に `denyinet on|off` を書いて `apps/system/denyinet` として同梱した。`wg up`(wg0 の UDP は GS2200M 経由で作る)→ `denyinet on` → `telnetd &` / `webserver &` の順。

ただし gs2200m デーモンの `SIOCDENYINETSOCK` 処理は、フラグを更新した後そのままドライバの `GS2200M_IOC_IFREQ` にも転送してしまい、ドライバが知らない cmd なので `-EINVAL`、デーモンは `ioctl()` の戻り値 `-1` をそのまま result に入れるため呼び出し側には `EPERM` に見える(フラグ自体は立っている)。`Dockerfile` でデーモンを `drvreq = false` に直した。

**4. webserver が `/mnt` のディレクトリ一覧を返す**

`spresense:wifi` の httpd は `SENDFILE` + `DIRLIST` 設定。esp32s3 と同じ組み込みページを出すため `CLASSIC` + `SCRIPT` 有効に切り替えた。

**upstream に報告すべきもの(GS2200M 側):** (a) `gs2200m_ioctl_ifreq()` が `ifr_name` を見ない、(b) デーモンが `SIOCDENYINETSOCK` をドライバに転送する。ドラフトは [docs/upstream/gs2200m-usrsock-issue-draft.md](../upstream/gs2200m-usrsock-issue-draft.md)。

### 追記 (2026-09-17): NuttX master でも Spresense 実機を通す — RTC_HIRES 起動回帰の特定と修正

upstream に出すには master で動くことが要る。`--build-arg NUTTX_REF=master`(bda22516, 2026-09-17)で `spresense-wifi` を作り直したところ、**ビルドは通るがコンソールに何も出ず NSH に到達しない**。ASSERT も無し、CPU はアイドル。`spresense:nsh` は起動する。

**切り分け:** Kconfig を一つずつ外す bisect(GS2200M、ストレージ、LCD、オーディオ、USB、拡張ボード、ELF、スタックサイズ、`STANDARD_SERIAL` — どれも無関係)の末、`CONFIG_RTC_HIRES` で ON/OFF が切り替わった。`cxd56_bringup.c` に `_err()` のトレースマーカーを入れると、`board_power_setup()` の中の `board_clock_initialize()` の直後で止まっている。

**原因(循環):**

1. master の `sched_processtick.c` は watchdog を `wd_timer(clock_systime_ticks())` で回す(12.7.0 はスケジューラ自身の tick カウンタ)。`CONFIG_RTC_HIRES=y` ではこの値が `clock_systime_timespec()` = RTC 由来
2. `clock_systime_timespec()` は `g_rtc_enabled` が立つまで `{0, 0}` を返す → 毎 tick `wd_timer(0)`、watchdog が一つも満了しない
3. cxd56 は `CONFIG_CXD56_RTC_LATEINIT` で外部 RTC の同期待ちを **watchdog の再試行(200 ms × 15)**でやり、そのコールバックで `g_rtc_enabled` を立てる

RTC を有効化する watchdog は RTC が有効になるまで満了しない。おまけに `board_power_control()` が `nxsched_usleep(1)` で絶対 tick 待ちに入るので、起動スレッドはそこで永久に寝る(マーカーが指した場所)。

最初は「`up_rtc_settime()` が `g_rtc_lock` を取ったまま `cxd56_rtc_count()`(同じロック)を呼ぶ再帰スピンロック」を疑ったが、`CONFIG_SPINLOCK` 無しでは `irqsave` の入れ子に落ちるだけで実害なし。SMP/SPINLOCK 構成では本物の問題なので `_nolock` 版を使う一行修正は Dockerfile に残してある。

**修正:** `clock_systime_timespec.c` の RTC 未有効時の分岐で `{0, 0}` の代わりに `clock_ticks2time(ts, clock_get_sched_ticks())` を返す。`cxd56_rtc_initialize()` は元々「RTC 有効化前の経過時間が `clock_systime_timespec()` で得られる」前提で offset を組んでいるので、契約の変更ではなく本来の期待値。Dockerfile の base ステージで、パターンがある時だけ当てる(12.7.0 は無反応)。

**結果(実機):** master で NSH 起動、rcS チェーン(`gs2200m` → `wg setconf` → `denyinet on` → `telnetd`)完走、Windows クライアントとのハンドシェイク約 10 s、トンネル越し telnet と HTTP 200 を確認。sim の回帰スクリプトも master で通る。**WireGuard 側のソースは 12.7.0 と master で一切変えていない。**

upstream 向けのドラフトは [docs/upstream/rtc-hires-wdog-regression-draft.md](../upstream/rtc-hires-wdog-regression-draft.md)。

### 追記 (2026-09-17 朝): ESP32-S3 も master で通す — defconfig の変化に 3 つ引っかかる

ビルド途中でホストの C: が一杯になり Docker Desktop の WSL VM ごと落ちた(`docker_data.vhdx` が 49 GB。bisect 用イメージとビルドキャッシュ)。イメージ整理と `Optimize-VHD` で 22 GB に戻してから再開。

master (bda22516) の `esp32s3-devkit:wifi` はビルドも Wi-Fi 接続も通るが、**電源投入だけでは `wg0` も telnetd も上がらず、`wg saveconf` は壊れる**。WireGuard 側ではなく defconfig 側の変化が 3 つ:

**1. NxInit がエントリポイントになった** — `CONFIG_INIT_ENTRYPOINT="init_main"` + `CONFIG_SYSTEM_NXINIT=y`(Android 風の `/etc/init.d/init.rc`)。init.rc が起動するコンソールの `sh` は `nsh_system_ctty()` で、`nsh_initialize()` を通らない。つまり `rc.sysinit` / `rcS` も `nsh_telnetstart()` も実行されない。12.7.0 と同じ `nsh_main` に戻す(Dockerfile、`CONFIG_SYSTEM_NXINIT=y` のときだけ)。

**2. `DEFAULT_TASK_STACKSIZE` が 4096 → 2048** — `wg` builtin のスタックがこれに追従していたため、`wg saveconf`(stdio + SPIFFS)が 2048 バイトを突き破る。症状はまず errno がゴミ(-135 / -257)、次に `wg` タスクの load/store 例外(`VADDR 70000009`)。`CONFIG_NET_WIREGUARD_STACKSIZE` の既定を `DEFAULT_TASK_STACKSIZE` 依存から **4096 固定**に変更(Kconfig の help に経緯を記載)。telnetd デーモンも 2048 になっていたので Dockerfile で 4096 に。

**3. 12.7.0 で作った SPIFFS が master で読めない** — `CONFIG_SPIFFS_NAME_MAX` が 128 → 32 に変わり、オブジェクトヘッダの寸法が変わる。旧イメージが残った `/data` を master がマウントすると、`echo > /data/x` は通るのに `fopen()` からの書き込みが `SPIFFS_ERR_DELETED`(-257、`spiffs_map_errno()` を通らず素通り)や `EFTYPE`(-135)で失敗し、以後 `echo` も失敗する。`esptool erase_region 0x180000 0x100000` で消して起動し直せば正常(フォーマットし直される)。**12.7.0 と master を行き来するときは `/data` を消す**こと。保存していた秘密鍵は失われるので、ピアの公開鍵を Windows 側で更新する必要がある。

上の 2 は upstream に出す前提で踏んでおいてよかった類(`DEFAULT_TASK_STACKSIZE` に既定を委ねる builtin は、小さい defconfig でそのまま落ちる)。1 と 3 は NuttX 側の方針変更で、こちらはドキュメント対応。

**結果(実機、master bda22516):** 電源投入 → rcS で `wg setconf /data/wg0.conf` → `wg0` up → telnetd 自動起動。Windows クライアントとのハンドシェイク、トンネル越し ping(6〜9 ms)、telnet、`webserver &`、HTTP 200(ボード情報テーブル)。**これで ESP32-S3 と Spresense の両方が 12.7.0 と master の両方で実機確認済み。** WireGuard 側のソース変更は `CONFIG_NET_WIREGUARD_STACKSIZE` の既定値のみ。

### 追記 (2026-09-17 午前): 既定を最新リリース 13.0.1 に切り替え — 12.7.0 / 13.0.1 / master の 3 本で実機確認

「12.7.0 で確認済み」は 6 リリース前(12.8 → 12.13 → 13.0.0 → 13.0.1)の話になっていたので、既定の `NUTTX_REF` を **`nuttx-13.0.1`(cec617df)** に上げた。12.7.0 は `--build-arg NUTTX_REF=nuttx-12.7.0` で今まで通り作れる(README に並記)。

13.0.1 で確認したこと:

- 全ステージ(sim / qemu / esp32 / esp32s3 / spresense / spresense-wifi)がビルドできる。sim の 3 本の回帰スクリプトと QEMU の検証スクリプトが PASS(12.7.0 / 13.0.1 / master の 3 本すべて)
- QEMU は 2 点直した。(a) 13.0 以降はリンク後処理が Python の `cxxfilt` を要求する。(b) `qemu-armv7a:nsh` が `.text` を flash(0x0)、`.data` を RAM 先頭 0x40000000 に置く構成になり、QEMU が DTB を置く場所(RAM 先頭)と `.data` が衝突して `fdt_get()` が無効 → virtio-net が登録されず `eth0` が出ない。upstream の `full` 構成に倣って `RAM_START=0x40200000` にして先頭 2 MB を DTB に空けた(リンカスクリプトが ROM 領域を使うときだけ。`CONFIG_BOOT_RUNFROMFLASH` は 12.7.0 でも y なので判定に使えず、最初それで 12.7.0 側を壊した)
- **ESP32-S3・Spresense とも実機で完走**(ヘッドレス起動 → ハンドシェイク 5 s → telnet → HTTP 200、デモページに `nuttx-13.0.1, cec617df`)
- **`CONFIG_RTC_HIRES` の起動回帰は 13.0.1 のリリースにも入っている**(`sched_processtick.c` が `wd_timer(clock_systime_ticks())` になっている)。Dockerfile のパッチがそのまま当たって起動する。リリースに入った回帰なので upstream 報告の優先度が上がった
- ESP32-S3 側: 13.0.1 には NxInit は無い(`INIT_ENTRYPOINT=nsh_main`)が、`DEFAULT_TASK_STACKSIZE=2048` と `SPIFFS_NAME_MAX=32` は入っている。SPIFFS は master と同じ形式なので、master で作り直した `/data` はそのまま読めた(鍵の変更なし)
- Spresense は起動時に `cxd56_farapiinitialize: Mismatched version: loader(20585) != Self(20596)` と出るようになった。ボードの GNSS ローダ FW が SDK より古いという警告で、Wi-Fi / WireGuard には無関係

---

## トンネル越し telnet で見つかった TCP 特有バグの調査・修正

ESP32-S3 実機でのハンドシェイク・ping 成功後、実用的なリモートアクセスのデモとして NuttX 標準の `telnetd`(NSH の `nsh_telnetstart` により起動時に自動起動済み)をトンネル越しに使えるか試したところ、**ICMP(ping)は正常なのに TCP(telnet)だけデータが一切届かない**という現象に遭遇した。

### 症状の切り分け

- `ping 10.10.0.2`(トンネル越し)は 0% packet loss で成功する
- トンネル越しに `telnet 10.10.0.2` すると **TCP の 3-way ハンドシェイクは成立する**(`connected=True`)が、telnetd のバナー(`NuttShell (NSH) NuttX-12.7.0`)が **1バイトも届かない**
- 同じテストスクリプトで **WireGuard を使わず同一 LAN 上で直接** `192.168.0.152:23` に telnet すると、バナー・コマンド応答とも正常に届く

これにより「telnetd 自体やプレーンな TCP スタックの問題ではなく、`wg0` の実装のうち TCP のデータ送出パスにだけ影響するバグ」であることを切り分けた。

### 原因調査: TX パスへのデバッグ計装

`nuttx-wireguardif.c` の `wg_txavail()` / `wg_txavail_work()` / `wg_txpoll()` / `wg_encrypt_and_send()` に `ninfo()` でトレースを仕込み、`CONFIG_DEBUG_NET_INFO=y` を有効にした状態でシリアルコンソールをキャプチャしながら再現させたところ、決定的な行が見つかった:

```
[CPU1] wg_txpoll: WGDBG txpoll: d_len=72 d_iob=0x3fc96f8c
[CPU1] wg_txpoll: WGDBG txpoll: proto=6 dest=01000a0a peer=0x3fc9a720
[CPU1] wg_encrypt_and_send: WGDBG encrypt_and_send: sendto total_len=112 ret=-1 errno=9
[CPU1] wg_txpoll: WGDBG txpoll: encrypt_and_send len=72 sent=0
```

`errno=9` = `EBADF`(不正なファイルディスクリプタ)。`sendto()` が呼ばれる場所ごとに成功・失敗が明確に分かれていた:

- **成功する呼び出し**: `wg_rx_task()` 自身のコンテキストから行われるもの — 受信した UDP パケットの処理(ハンドシェイク応答、keepalive)、および `wg_inject_plaintext()` が `ipv4_input()` 呼び出し中に同期的に生成される即時応答(ICMP echo reply、TCP の SYN-ACK)を捕まえて送り返す経路
- **失敗する呼び出し**(`errno=9`): `wg_txavail()` が `work_queue(LPWORK, wg_txavail_work, ...)` で非同期にスケジュールする `wg_txpoll()` 経由の送信 — つまり telnetd セッションタスクなど、**別タスクが `send()`/`write()` した TCP アプリケーションデータ**すべて

ICMP echo reply と TCP の SYN-ACK は `wg_inject_plaintext()` が `ipv4_input()` 呼び出しのその場で同期的に構築・送信するため `wg_rx_task` 自身のコンテキストで完結する一方、telnetd がバナーを `write()` する処理は非同期にキューされ、システムの **LPWORK ワーカースレッド上で** `wg_txpoll()` → `wg_encrypt_and_send()` → `sendto()` が呼ばれる。これが症状(ハンドシェイクは通るのに TCP データだけ届かない)と完全に一致した。

### 根本原因

NuttX のファイルディスクリプタは **タスクグループごとにスコープされる**。`priv->sock`(`wg_initialize()` 内で `socket()` により作成)は、`wg_rx_task` が `task_create()` で生成される際に(生成元タスクから)継承されるため `wg_rx_task` 自身からは有効に使えるが、**LPWORK は起動時から存在する独立したシステムワーカータスクであり、`wg_rx_task` や「wg」NSH コマンドタスクとは `task_create()` の親子関係が一切ない**。そのため LPWORK のファイルディスクリプタテーブルには `priv->sock` の fd 番号に対応するエントリが存在せず、そこから `sendto(priv->sock, ...)` を呼ぶと `EBADF` になる。

Phase 3 で見つかった「`SO_RCVTIMEO` が効かない」「detached pthread が生成元タスクの終了とともに死ぬ」というバグと合わせて、**fd(ファイルディスクリプタ)やタスクのライフタイムに関する前提が sim/QEMU では表面化しなかった NuttX 特有の落とし穴**という点で同系統の問題だった。

### 修正: `psock_*()` 内部 API への切り替え

NuttX には、ファイルディスクリプタテーブルを一切経由しない `struct socket` ベースの内部 API(`psock_socket()` / `psock_bind()` / `psock_sendto()` / `psock_recvfrom()` / `psock_close()`)が公開されている(`include/nuttx/net/net.h`)。`struct socket` は単なるメモリ上の構造体で、どのタスクからポインタ経由で触っても問題ない — セマフォと同じ扱いができる。

`nuttx-wireguardif.c` を以下のように変更した:

- `struct wg_netdev_s` の `int sock` を `struct socket psock` に変更
- `wg_initialize()`: `socket()`/`bind()`/`close()` → `psock_socket()`/`psock_bind()`/`psock_close()`
- `wg_encrypt_and_send()`・`wg_start_handshake()`・`wg_send_handshake_response()`: `sendto()` → `psock_sendto()`(`wg_txpoll()` 経由・LPWORK コンテキストも含め、呼び出し元に関わらず動作する)
- `wg_rx_task()`: `poll()` + `recvfrom()` を、`psock_recvfrom(..., MSG_DONTWAIT, ...)` を `usleep(WG_RX_POLL_MSECS)`(50ms)間隔で回すループに変更(`poll()` も fd ベースで同じ制約を受けるため。`wg_run_timers()` はタイムスタンプの期限切れ判定で駆動されるので、呼び出し頻度を上げても副作用はない)

### 実機での修正確認

修正後の ESP32-S3 実機で、Windows 公式クライアントとのトンネル越し telnet セッションが完全に動作することを確認した:

```
Pinging 10.10.0.2 with 32 bytes of data:
Reply from 10.10.0.2: bytes=32 time=63ms TTL=128
Reply from 10.10.0.2: bytes=32 time=107ms TTL=128
Reply from 10.10.0.2: bytes=32 time=91ms TTL=128
Packets: Sent = 3, Received = 3, Lost = 0 (0% loss)

==== telnet demo ====
BANNER:

NuttShell (NSH) NuttX-12.7.0
nsh>
---- uname -a ----
NuttX  12.7.0 5d8cdeae-dirty Aug 16 2026 22:49:33 xtensa esp32s3-devkit
nsh>
---- uptime ----
00:01:38 up  0:01, load average: 0.00, 0.00, 0.00
nsh>
---- free ----
                 total       used       free    maxused    maxfree  nused  nfree
      Umem:     291648      93168     198480      94440     198432    201      2
nsh>
```

トンネル越しに TCP(telnet)でコマンドを送り、実際に NuttX 側で実行された結果(`uname -a`・`uptime`・`free`)が正しく返ってきている。ICMP だけでなく TCP を含む任意のアプリケーション通信がトンネル越しに動作することを実証できた。

修正は `nuttx_port/apps/netutils/wireguard/nuttx-wireguardif.c` 側のみで、sim/QEMU の既存ビルドにも同じ修正が反映される(`docker build --target sim` で再ビルド・コンパイル成功を確認済み)。

### デモ用 Web サーバー

telnet でのコマンド実行に加え、NuttX 標準の uIP webserver(`apps/netutils/webserver` + `apps/examples/webserver`)もトンネル越しに動かせることを確認した。`esp32s3` ステージに `CONFIG_NETUTILS_WEBSERVER` / `CONFIG_EXAMPLES_WEBSERVER` を有効化し、デモ用にブランディングしたページ(`docker/webserver-demo/header.html` / `index.shtml`)を `apps/examples/webserver/httpd-fs/` に上書きコピーするようにした。

```
nsh> webserver &
Starting webserver
```

以降、トンネルの反対側のブラウザから `http://10.10.0.2/`(ポート 80、`webserver` の既定ポート)でアクセスできる。ICMP・対話的 TCP(telnet)・HTTP という3種類の通信すべてがトンネル越しに動作することの実証になった。この一連の流れ(telnet ログイン→コマンド実行→`webserver &`→ブラウザアクセス)をデモ動画として収録した: [docs/phase4-summary.md](phase4-summary.md) を参照。

---

## 長時間動作で観測したクラッシュ — Wi-Fi ドライバ側 (2026-08-30)

ヘッドレス運用中に ESP32-S3 がネットワークから消える事象が2度あった。1度目(2026-08-29)は
USB を繋いでいなかったためコンソールが読めず、クラッシュ・Wi-Fi 切断・電源断のどれかを
区別できなかった。2度目はシリアルを記録しながら回していたため、**原因を特定できた**。

記録は [logs-esp32s3-wifi-crash.txt](logs-esp32s3-wifi-crash.txt) に保存してある。

### 発生状況

**4時間28分の連続稼働後**にクラッシュ。それまでトンネル越しに約 490 KB を送信し続けており
(1分ごとの ping + telnet セッション)、直前まで劣化の兆候は無かった。

### クラッシュ内容

```
xtensa_user_panic: User Exception: EXCCAUSE=001c task: wifi
up_dump_register:    PC: 42029e05
up_dump_register:    A8: ffffffe0
up_dump_register:   SAR: 00000018 CAUSE: 0000001c VADDR: ffffffec
```

`EXCCAUSE=0x1c` は LoadProhibited(不正なアドレスからのロード)。
`addr2line` で呼び出し経路を解決すると:

```
start_rt_timer              ← ここで例外 (arch/xtensa/src/esp32s3/esp32s3_rt_timer.c)
  esp32s3_rt_timer_start
  esp_timer_arm             (esp32s3_wifi_adapter.c)
  sta_reset_beacon_timeout
  pm_rx_beacon_process
  pm_on_beacon_rx
  ppTask                    (Espressif Wi-Fi バイナリ)
```

**クラッシュしたのは `wifi` タスクで、`wg_rx` ではない。** Wi-Fi の省電力処理が
ビーコン受信時にビーコンタイムアウトのタイマーを張り直す経路で落ちている。

### 原因の所在

`start_rt_timer()` はタイマーリストを走査する:

```c
list_for_every_entry(&priv->runlist, temp_p, struct rt_timer_s, list)
  {
    if (temp_p->alarm > timer->alarm)   /* ← ここで例外 */
```

`struct rt_timer_s` の `list` メンバはオフセット 32 にある。レジスタ **A8 が `0xffffffe0`**、
すなわち `0 - 32` になっており、これは `container_of(NULL, struct rt_timer_s, list)` の結果に
ほかならない。つまり **`priv->runlist` に NULL リンクが混入していた**(リスト破壊)。

**これは NuttX の ESP32-S3 プラットフォームコード側の問題であり、WireGuard 実装とは無関係。**
`esp32s3_rt_timer_start()` 自体は `spin_lock_irqsave()` を取っているが、同ファイル内には
`enter_critical_section()` を使う経路も混在しており、その組み合わせが疑わしい。ただし
競合の正確な経路までは特定できていないため、断定はしない。

### upstream では、この実装ごと置き換わっている

NuttX master を確認したところ、**`esp32s3_rt_timer.c` は存在しない**。同名の実装が残っているのは
`arch/risc-v/src/esp32c3-legacy/` だけで、名前のとおり非推奨扱いになっている。

置き換え先は共通 Espressif 層の `esp_hr_timer.h` で、中身はこうなっている:

```c
/* This is a compatibility wrapper for the new ESP-HAL timer adapter */
#include "esp_timer_adapter.h"
```

つまり **NuttX は自前のリンクリスト実装(今回クラッシュした `start_rt_timer()` そのもの)を捨て、
Espressif の HAL タイマーに委譲する方式へ移行している**。今回踏んだコードは upstream には
もう無い。

したがって本件は「12.7.0 期のコードに残っていた不具合で、upstream では実装ごと差し替え済み」
という位置づけになる。対応方針としては:

- **upstream にバグ報告する価値は低い** — 該当コードが既に無いため
- **新しい NuttX へ移行すれば解消する可能性が高い**。ただし未確認なので、長時間動作で
  再現しないことを実際に確かめる必要がある
- 12.7.0 に留まる場合、この Wi-Fi クラッシュは既知の制約として受け入れることになる

Pico 2 W の検証で既に NuttX master を使っているため、**バージョン固定を外す判断とも関係する**。

### 副産物: `wg_rx` のスタック実測値が更新された

クラッシュダンプにはタスク一覧も含まれており、そこで自分の過去の計測が甘かったことが判明した:

```
dump_task:  10  10  0 100 RR Task - Waiting Semaphore ... 4056  3392  83.6%!   wg_rx
```

短時間の負荷試験では **2,960 バイト (72.9%)** だったが、長時間動作では **3,392 バイト (83.6%)**
まで伸びており、NuttX の `!` 警告が出ていた。深い経路に入るかどうかは「パケットが到着した
瞬間のスタック状態の組み合わせ」に依存するため、**短時間のバーストは worst case にならない**。

`CONFIG_NET_WIREGUARD_RX_STACKSIZE` の既定値を **4096 → 6144** に引き上げ、Kconfig の help にも
「数値を信じる前にボードを長時間走らせること」を明記した。

---

## 学んだこと・引き継ぎ事項

### 良かった点

- WireGuard コンポーネントのコードは **sim/qemu 向けに書いたものが ESP32・ESP32-S3・Spresense 実機ターゲットでも変更なしでビルドできる**ことを確認できた。プラットフォーム抽象化(`nuttx-platform.c`)と netdev 統合(`nuttx-wireguardif.c`)の設計がポータブルであることの実証になった
- 各ターゲット固有の Kconfig ギャップ(ESP32: なし、Spresense: `CONFIG_NET`/`CONFIG_SCHED_WORKQUEUE` 未有効)を発見・解消し、`Dockerfile` の `esp32`/`esp32s3`/`spresense` ステージとして再現可能な形で残せた
- **ESP32-S3 実機で実 Wi-Fi・実ピア(Windows 公式クライアント)との WireGuard ハンドシェイク・トンネル ping を確認**。sim・QEMU の仮想ネットワークだけでなく、本物のネットワーク環境・本物の異実装ピアとの相互運用性まで実証できた
- 無印 ESP32-WROOM-32 のブートモード問題は、ESP32-S3(別個体・別ボード)では一切発生しなかった。チップ世代の違いというより個体/ボード側の問題だった可能性が高い

### 未解決

- ESP32-WROOM-32: 実機のブートモード切り替え(ハードウェア側の問題の疑い、上記の通り ESP32-S3 では再現しなかった)
- ~~Spresense: USB 列挙が発生しない~~ → **解決。** CP210x ドライバ未インストールによる誤診断だった
- ~~Spresense: 実ピアとのハンドシェイク未確認~~ → **解決(2026-09-16)。** iS110B Wi-Fi Add-on 経由で handshake・ping 4/4。残りは Spresense でのトンネル越し TCP、`wg genkey` の決定論的シード、GS2200M ドライバの `ifr_name` 無視の upstream 報告
- 両方とも、次回は「別の PC で試す」「別のケーブル・電源で試す」など、より切り分けの効く環境で再挑戦する必要がある
- ESP32-S3 側で長時間 keepalive・再接続・複数 peer など異常系の検証はまだ(sim/QEMU と同様、短時間の handshake + ping のみ確認済み)
