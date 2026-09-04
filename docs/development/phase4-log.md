# Phase 4 開発ログ: 実機ビルド・書き込み・実ネットワーク検証

## 目標

実際に手元にある実機(ESP32-WROOM-32、ESP32-S3、Sony Spresense メインボード)で `CONFIG_NET_WIREGUARD=y` のビルドを通し、書き込み・起動・実ネットワーク越しの WireGuard 通信まで確認する。

**結論を先に:** **ESP32-S3 で完全成功。** 実機を Wi-Fi 経由で実際のアクセスポイントに接続し、Windows 上の公式 WireGuard クライアントと実際にハンドシェイクを成立させ、トンネル越しの ping(0% packet loss)まで確認できた。さらに telnetd をトンネル越しに使おうとした際に見つかった「TCP のアプリケーションデータだけがトンネルを通らない」バグ(原因: LPWORK ワーカースレッドから `sendto()` する際にファイルディスクリプタがそのスレッドのタスクグループに属さず `EBADF` になっていた)を特定・修正し、トンネル越し telnet セッションでのコマンド実行まで実機で確認した(詳細は後述の節)。**Spresense (ARM Cortex-M4F) でも実機起動・`wg0` 起動を確認した** — 当初「ハードウェア故障の疑い」としていたが、実際には CP210x ドライバ未インストールが原因の誤診断で、後日訂正した(詳細後述)。ESP32-WROOM-32 のみ、GPIO0 経路の故障により書き込みに到達できていない。

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

**未確認:** Spresense メインボードには Wi-Fi が内蔵されていないため、実ピアとのハンドシェイク・トンネル疎通は依然として検証できない(別売りの GS2200M 拡張モジュールが必要)。ここで確認できたのは「wg0 が上がる」ところまで。

なお起動時に `cxd56_farapiinitialize: Mismatched version: loader(20585) != Self(20591)` の警告が出るが、NSH の動作および `wg` の動作には影響していない。Sony 提供のローダ/GNSS ファームを更新すれば消えるはず。

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
- ~~Spresense: USB 列挙が発生しない~~ → **解決。** CP210x ドライバ未インストールによる誤診断だった。実機で NuttX 起動・`wg0` 起動・`wg` の各サブコマンド動作を確認済み。ただし Wi-Fi 非搭載のため実ピアとのハンドシェイクは未確認(GS2200M 拡張モジュールが必要)
- 両方とも、次回は「別の PC で試す」「別のケーブル・電源で試す」など、より切り分けの効く環境で再挑戦する必要がある
- ESP32-S3 側で長時間 keepalive・再接続・複数 peer など異常系の検証はまだ(sim/QEMU と同様、短時間の handshake + ping のみ確認済み)
