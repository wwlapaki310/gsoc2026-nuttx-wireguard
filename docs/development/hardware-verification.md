# 実機検証手順(ESP32-S3 / Raspberry Pi Pico 2 W / ESP32-WROOM-32 / Sony Spresense)

## 現在のステータス

**ESP32-S3 で実機検証が完全に成功した。** Wi-Fi 経由で実際のアクセスポイントに接続し、Windows 上の公式 WireGuard クライアントとの実ハンドシェイク・トンネル越し ping(0% packet loss)まで確認済み。詳細は [docs/phase4-log.md](phase4-log.md) の「ESP32-S3 — 実機での完全成功」を参照。

さらに、telnetd をトンネル越しに使う実用デモの過程で「TCP のアプリケーションデータだけがトンネルを通らない」バグ(LPWORK ワーカースレッドから `sendto()` する際に fd が `EBADF` になっていた)を発見・修正し、トンネル越し telnet セッションでのコマンド実行(`uname -a`・`uptime`・`free` など)、および `webserver &` で起動した uIP webserver へのトンネル越しブラウザアクセスまで実機で確認済み。詳細・デモ動画は [docs/phase4-log.md](phase4-log.md) と [docs/phase4-summary.md](phase4-summary.md) を参照。

**Sony Spresense (ARM Cortex-M4F) + iS110B Wi-Fi Add-on ボードでも、実 Wi-Fi 経由で Windows 公式クライアントとのハンドシェイク・トンネル越し ping(4/4)を確認済み。** GS2200M は ESP32 の `wlan0` とは違う `usrsock` プロキシ方式のドライバで、この環境特有のバグ(`wg0` 向け ioctl が usrsock に横取りされる)を1つ修正した。当初「USB デバイスとして列挙されない」としていたのは CP210x ドライバ未インストールによる誤診断で、後日訂正した。

ESP32-S3 と Spresense はどちらも **NuttX 12.7.0 と NuttX master(bda22516、2026-09-17)の両方**で同じ手順(ヘッドレス起動 → ハンドシェイク → telnet → HTTP)を実機確認済み。WireGuard 側のソース差分は `CONFIG_NET_WIREGUARD_STACKSIZE` の既定値(4096 固定)だけ。master では NuttX 本体側に cxd56 の `CONFIG_RTC_HIRES` 起動回帰があり `Dockerfile` がビルド時に修正を当てている([docs/upstream/rtc-hires-wdog-regression-draft.md](../upstream/rtc-hires-wdog-regression-draft.md))。ESP32-S3 側の defconfig 変化(NxInit、スタック既定 2048、SPIFFS の非互換)への対応は各ボードの「ビルド」節と [phase4-log.md](phase4-log.md) の 2026-09-17 追記を参照。

ESP32-WROOM-32 のみ、GPIO0 を Low にする経路の故障によりダウンロードモードに入れず、書き込みに到達できていない(ビルド自体はコード変更なしで成功)。詳しい経緯は同じく [docs/phase4-log.md](phase4-log.md) を参照。

Raspberry Pi Pico 2 W は、本リポジトリが固定している NuttX 12.7.0 では RP2350/Pico 2 系のボード定義が存在しないため、そのままではビルド対象にできない。Apache NuttX master では `raspberrypi-pico-2` ボードとして USB NSH の起動が確認でき、さらに本リポジトリの WireGuard 実装を移植して `wg` builtin と `wg0` TUN インターフェースの起動まで確認した。その後、公式 Raspberry Pi Pico 2 W 向け Wi-Fi bringup の [apache/nuttx#19250](https://github.com/apache/nuttx/pull/19250) も試したが、CYW43439 の GSPI 初期化で ready レジスタが `0xffffffff` となり、Wi-Fi 接続前で止まっている。

---

## スコープについて

- **Raspberry Pi Pico / Pico 2 (無印)**: ネットワーク機能を持たないため対象外
- **Raspberry Pi Pico W / Pico 2 W**: Pico W(RP2040) と Pico 2 W(RP2350) はどちらもオンボード Wi-Fi チップとして CYW43439 を使う。NuttX master には CYW43439 系ドライバと RP2350 ボード定義が入っているが、本リポジトリが固定している NuttX 12.7.0 には Pico 2 W をそのまま使うためのボード定義が無い。公式 Raspberry Pi Pico 2 W ボードとしての Wi-Fi bringup は [apache/nuttx#19250](https://github.com/apache/nuttx/pull/19250) で PR 中。こちらでは PR #19250 を試し、USB シリアル経由の NuttX 起動、`wapi`/`wg` builtin、`wg0` 起動まで確認済み。Wi-Fi は `wlan0` 登録後、GSPI 初期化で `0xffffffff` を読み続けて `ifup wlan0` が `-ENODEV` になるため未接続
- **Sony Spresense**: 当初はプロポーザル上「著者の業務経験」として触れているのみでロードマップ外だったが、実機を保有しているため対応した。**Spresense 本体には Wi-Fi が内蔵されていない**ため、別売りの Wi-Fi Add-on ボード iS110B(GS2200M、NuttX のボードコンフィグ `spresense:wifi`)を載せて実 Wi-Fi 経由の疎通まで確認した
- **ESP32-S3**: プロポーザル上の当初の実機ターゲット。**実機検証完了**(下記参照)

---

## ESP32-S3(確認済み・完全成功)

### 前提条件

- ESP32-S3 搭載ボード(Freenove 製、16MB フラッシュ、PSRAM 8MB 内蔵で確認)
- USB ケーブル(データ通信対応のもの)
- ESP32-S3 はチップにネイティブ USB を内蔵している(ただし今回のボードは WCH 製 CH343 ブリッジチップ経由の接続だった。それでも無印 ESP32 のようなブートモード問題は発生しなかった)

### ビルド

```bash
docker build --target esp32s3 \n  --build-arg WIFI_SSID=<ssid> --build-arg WIFI_PASS=<passphrase> \n  -t nuttx-wireguard:esp32s3 .
```

Wi-Fi の認証情報はビルド引数で渡す(`CONFIG_NETINIT_WAPI_SSID/PASSPHRASE` と `CONFIG_NETINIT_DHCPC` を設定する)。渡さなければ defconfig のプレースホルダーのまま。

NuttX master で作る場合は `--build-arg NUTTX_REF=master` を足す(2026-09-17 時点 bda22516 で実機確認済み)。master の `esp32s3-devkit:wifi` は defconfig が変わっており(NxInit エントリポイント、`DEFAULT_TASK_STACKSIZE=2048`、`SPIFFS_NAME_MAX=32`)、Dockerfile がヘッドレス起動に必要な分を吸収している。**12.7.0 のイメージから master(またはその逆)に書き換えるときは `/data` の SPIFFS を消す**こと — `SPIFFS_NAME_MAX` が違うので旧イメージのままだと書き込みが `EFTYPE` で壊れる:

```bash
python -m esptool -c esp32s3 -p COM7 erase_region 0x180000 0x100000
```

保存していた `wg0.conf`(秘密鍵)も消えるので、`wg genkey` からやり直して Windows 側のピア公開鍵を更新する。

`esp32` ステージとほぼ同じ構成で、ツールチェインを `xtensa-esp32s3-elf`、ボードを `esp32s3-devkit:wifi` に変更したもの。`esp32s3-devkit:wifi` は NSH + `CONFIG_ESP32S3_WIFI` + WAPI が最初から有効になっている構成。WireGuard 用 Kconfig(`ALLOW_BSD_COMPONENTS`・`NET_TUN`・`NET_SOCKOPTS`・`NET_WIREGUARD`・`DEV_URANDOM_ARCH`)を追加してビルド。

Wi-Fi 認証情報(`CONFIG_NETINIT_WAPI_SSID`/`PASSPHRASE`)・WireGuard 秘密鍵(`CONFIG_NET_WIREGUARD_PRIVATE_KEY`)・ピア設定(`CONFIG_NET_WIREGUARD_PEER_*`)はビルド前に `kconfig-tweak --set-str` で実際の値に上書きする(デフォルトはプレースホルダー/空)。`CONFIG_NETINIT_DHCPC=y` も有効化しておくと実際のルーターから IP を取得できる。

### 書き込み(確認済み)

```bash
docker create --name esp32s3extract nuttx-wireguard:esp32s3
docker cp esp32s3extract:/opt/nuttx/nuttx.bin ./nuttx.bin
docker rm esp32s3extract

python -m esptool -c esp32s3 -p COM7 -b 921600 write_flash -fs detect -fm dio -ff 40m 0x0000 nuttx.bin
```

書き込みオフセットは無印 ESP32(`0x1000`)と異なり **`0x0000`**。COM ポート番号は環境に合わせる。デフォルトの自動リセットのみで書き込みでき、ボタン操作は不要だった。

### 動作確認(確認済み)

```
nsh> ifconfig
wlan0	Link encap:Ethernet HWaddr a4:cb:8f:df:e9:54 at RUNNING mtu 1500
	inet addr:192.168.0.152 DRaddr:192.168.0.1 Mask:255.255.255.0

nsh> wg
wg0 is up (listen port 51820)

nsh> wg show
peer: <Windows 側公開鍵>
  endpoint: 192.168.0.216:51820
  latest handshake: 11 seconds ago
  transfer: 336 B received, 240 B sent
```

Windows 側は Linux カーネル実装ではなく **公式 WireGuard for Windows クライアント**(`winget install WireGuard.WireGuard`)を使用し、異実装間の相互運用性も確認した。`ping 10.10.0.2`(ESP32-S3 のトンネルアドレス)で 4/4 パケット・0% packet loss を確認。詳細な手順・ログは [docs/phase4-log.md](phase4-log.md) を参照。

### ヘッドレス運用(USB シリアルなし・Wi-Fi のみ)— 確認済み

PC から USB を抜き、電源アダプタだけで動かした状態で、ネットワーク越しに操作できる。
成立させるには次の3つが必要だった。

| 必要なもの | 実現方法 |
|---|---|
| 起動時に `wg0` が上がること | `/etc/init.d/rcS` に `wg` を置く(下記) |
| 起動時に telnetd が上がること | `CONFIG_NSH_TELNET=y`(`nsh_init.c` が `nsh_telnetstart()` を呼ぶ) |
| 宛先アドレスが分かること | トンネル側 `10.10.0.2` は Kconfig 固定なので常に一定 |

**`wg` の自動実行:** `apps/nshlib/nsh_init.c` の起動順は
`netinit_bringup()` → `/etc/init.d/rcS` → `nsh_telnetstart()` なので、
rcS は「ネットワークが上がった後」に走る。ここに `wg` を1行置けば電源投入だけでトンネルが張られる。
rcS は ROMFS に焼き込まれ、`boards/Board.mk` の `RCSRCS` 経由でビルドされる
(esp32s3-devkit には元々 `src/etc/` が無いため、`docker/esp32s3-etc/` から配置し
`src/Make.defs` に `RCSRCS` を追記している。いずれも `Dockerfile` の `esp32s3` ステージが自動で行う)。

DHCP の完了を待つ必要はない。`wg` は `INADDR_ANY` に bind するだけで、
ハンドシェイクは `wg_rx` タスクが `REKEY_TIMEOUT` ごとに再試行するため、
アドレス取得が遅れても自動的に追いつく。

起動ログ(USB を抜く前に確認したもの):

```
*** Booting NuttX ***
wg0 is up (listen port 51820)     ← rcS が自動実行
telnetd [10:100]                  ← telnetd 自動起動

NuttShell (NSH) NuttX-12.7.0
nsh> ifconfig
wlan0	Link encap:Ethernet HWaddr a4:cb:8f:df:e9:54 at RUNNING mtu 1500
	inet addr:192.168.0.152 DRaddr:192.168.0.1 Mask:255.255.255.0

wg0	Link encap:TUN at RUNNING mtu 1420
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0
```

**2つの入り口が使える:**

```bash
telnet 192.168.0.152     # LAN 経由(DHCP アドレス)
telnet 10.10.0.2         # トンネル経由(Kconfig 固定・常に一定)
```

トンネル経由で `wg show` を実行し、トンネル自身の状態がトンネル越しに返ることまで確認済み。

**アドレスについて:** LAN 側は DHCP のままにしてある。
リースが MAC に対して安定しているため実運用では `192.168.0.152` から動かないが、
確実に固定したい場合はルータ側で DHCP 予約を入れるのが安全
(NuttX 側で静的 IP にすると、ルータの DHCP プールと衝突した際に
USB を挿し直さないと復旧できなくなる)。
**トンネル側 `10.10.0.2` は Kconfig 由来なので、そもそも DHCP に依存しない。**
ネットワークが変わっても変わらないアドレスとして使えるのが VPN を載せた効果でもある。

### 実行時設定と永続化

鍵・ピアはビルドに焼き込まなくてよい。`wg(8)` と同じサブコマンドで設定でき、
`wg(8)` と同じ INI 形式で保存できる。

```
nsh> wg genkey                                  # デバイス上で鍵を生成
<秘密鍵 44 文字。ここには実物を貼らないこと>

nsh> wg down
nsh> wg set private-key <上で生成した鍵>
nsh> wg set peer <対向の公開鍵> endpoint 192.168.0.216:51820 allowed-ips 10.10.0.1/32
nsh> wg up
wg0 is up (listen port 51820)

nsh> wg saveconf                                 # 電源断をまたいで残す
```

保存すると、次回起動時に rcS が `wg up` の前に読み込む。ファイルが無ければ
Kconfig の値にフォールバックするので、未設定のボードもそのまま起動する。

出力は本家と同じ形式なので、**デスクトップの WireGuard クライアントの設定ファイルと
相互に読める**:

```
[Interface]
PrivateKey = ...
ListenPort = 51820

[Peer]
PublicKey = ...
AllowedIPs = 10.10.0.1/32
Endpoint = 192.168.0.216:51820
PersistentKeepalive = 25
```

保存先は `CONFIG_NET_WIREGUARD_CONFIG_PATH`(既定 `/data/wg0.conf`)。
esp32s3-devkit は SPIFFS を `/data` にマウント済みなので追加設定は要らない。

> **注意:** `wg set` / `wg setconf` は **wg0 が down のときだけ**受け付ける
> (稼働中のピア差し替えは RX タスクと競合するため)。`wg down` してから設定し、
> `wg up` で戻す。
>
> また、`wg set peer` の行は 44 文字の base64 鍵を含むため約 134 文字になる。
> **`CONFIG_NSH_LINELEN` を 160 に上げていないと途中で切られる**ので注意
> (`Dockerfile` の各ステージでは設定済み)。

---

## Raspberry Pi Pico 2 W(USB NSH / `wg0` 起動確認済み)

### 現在確認できている範囲

今回確認できたのは、Pico 2 W を NuttX の `raspberrypi-pico-2` ボードとして起動し、USB シリアル上の NSH から WireGuard の `wg` builtin を実行して `wg0` を上げるところまで。

```
nsh> wg
wg0 is up (listen port 51820)

nsh> wg show
interface: wg0
  public key: qdgJgJ/SgN/WO82puRp0zCPsaWamZjSdALMq+86Ap1w=
  listening port: 51820

nsh> ifconfig
wg0	Link encap:TUN at UP mtu 1420
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0

nsh> uname -a
NuttX  0.0.0 5d2ed54d-dirty Aug 29 2026 11:16:29 arm raspberrypi-pico-2
```

これは `wg0` の netdev 登録、WireGuard 鍵の読み込み、TUN インターフェース起動が Pico 2 W 上でも成立することの確認である。その後、公式 Pico 2 W ボードの Wi-Fi bringup を含む [apache/nuttx#19250](https://github.com/apache/nuttx/pull/19250) も取り込んで試したが、現時点では Wi-Fi 接続前の GSPI 初期化で止まっている。

### 2026-09-04 追記: Pico 2 W Wi-Fi bring-up調査メモ

Pico 2 W は BOOTSEL 時に `RP2350`、NuttX 起動後に USB CDC `COM8` として認識できる。`raspberrypi-pico-2-w`(apache/nuttx#19250) + 本リポジトリの WireGuard アプリで、NSH、`wapi`、`wg` builtin までは起動確認済み。

一方、Wi-Fi は未接続。`wlan0` は登録されるが MAC が `00:00:00:00:00:00` のまま `DOWN`、`ifup wlan0` は `-ENODEV` で失敗する。デバッグログでは `bcmf_wl_active()` 内の GSPI 初期化で CYW43439 ready レジスタが `0xffffffff` となり、ready pattern を検出できていない。SSID/PSK/DHCP 以前の段階で止まっている。

切り分けとして、GSPI 1MHz 化、電源 ON 待ち時間延長、PR #19533 の DATA/CLK pull-down 修正、GPIO25 LED/CS 衝突回避を試したが、現象は変わらず。現時点では telnet/VPN over Wi-Fi は未到達。

### 前提条件

- Raspberry Pi Pico 2 W
- USB ケーブル(データ通信対応のもの)
- Windows ホストで確認
- Apache NuttX master / apps master
  - 本リポジトリの Dockerfile が固定している NuttX 12.7.0 には RP2350/Pico 2 系の `raspberrypi-pico-2` ボードが無い
  - 今回は upstream master の `boards/arm/rp23xx/raspberrypi-pico-2:usbnsh` をベースにした
  - 公式 Pico 2 W の `raspberrypi-pico-2-w` ボードは [apache/nuttx#19250](https://github.com/apache/nuttx/pull/19250) で PR 中
- Pico SDK / picotool
  - UF2 生成時に `PICO_SDK_PATH` が必要

### ビルド方針

現時点では本リポジトリの `Dockerfile` に Pico 2 W 用ステージは無い。確認時は、NuttX master と apps master を別途用意し、本リポジトリの `nuttx_port/apps/netutils/wireguard/` を `apps/netutils/wireguard/` にコピーしてビルドした。

ベースは USB NSH:

```bash
./tools/configure.sh raspberrypi-pico-2:usbnsh
```

追加で有効化した主な Kconfig:

```text
CONFIG_NET=y
CONFIG_NET_IPv4=y
CONFIG_NET_UDP=y
CONFIG_NET_TCP=y
CONFIG_NET_TUN=y
CONFIG_NET_TUN_PKTSIZE=1420
CONFIG_NET_SOCKOPTS=y
CONFIG_SCHED_WORKQUEUE=y
CONFIG_SCHED_LPWORK=y
CONFIG_DEV_URANDOM=y
CONFIG_DEV_URANDOM_XORSHIFT128=y
CONFIG_NET_WIREGUARD=y
CONFIG_NET_WIREGUARD_PRIVATE_KEY="<base64 private key>"
```

秘密鍵が空、または不正な値だと `wg` が `-22` (`EINVAL`) で失敗する。実機確認では最初にここで詰まり、有効な WireGuard 秘密鍵を入れ直したファームで `wg0 is up` まで進んだ。

### ビルド時の注意点

`raspberrypi-pico-2:usbnsh` はネットワーク機能を持たない構成なので、そのまま `CONFIG_NET=y` を有効にすると board 側の `arm_netinitialize()` が不足してリンクに失敗する。今回の `wg0` 起動確認では、Wi-Fi bringup ではなく TUN インターフェース単体の確認を目的としていたため、暫定的に no-op の `arm_netinitialize()` を追加してビルドした。

これは検証用の一時対応であり、公式の Pico 2 W 対応としては正しくない。Wi-Fi 経由の WireGuard 疎通を目指す場合は、`arm_netinitialize()` で CYW43439 を初期化し、`wlan0` を NuttX netdev として登録する board bringup が必要になる。

### UF2 の書き込み

ビルド成果物は `nuttx.uf2`。Windows では Pico 2 W を BOOTSEL モードで接続し、マスストレージとして見えたドライブへ UF2 をコピーする。

手順:

1. USB を抜く
2. BOOTSEL ボタンを押しっぱなしにする
3. 押したまま USB を挿す
4. Windows に `RP2350` ドライブが出たら BOOTSEL を離す
5. `nuttx.uf2` を `RP2350` ドライブへコピーする

PowerShell での確認例:

```powershell
Get-Volume | Where-Object { $_.FileSystemLabel -in @('RP2350','RPI-RP2') }
Copy-Item -LiteralPath .\pico2w-wireguard-validkey.uf2 -Destination D:\ -Force
```

コピーが完了すると Pico 2 W は自動的に再起動し、USB シリアルデバイスとして再列挙される。今回の環境では `USB シリアル デバイス (COM8)` として見えた。

### USB シリアルでの確認

シリアル条件は 115200 bps。Windows では COM 番号を環境に合わせる。

PowerShell から pyserial で簡易確認する例:

```powershell
python -c "import serial, time; s=serial.Serial('COM8',115200,timeout=1); s.write(b'\r\n'); time.sleep(1); print(s.read(4096).decode(errors='replace')); s.close()"
```

NSH が起きたら、以下を実行する。

```
nsh> help
nsh> wg
nsh> wg show
nsh> ifconfig
nsh> uname -a
nsh> free
```

期待する最小結果:

- `help` の Builtin Apps に `wg` が出る
- `wg` が `wg0 is up (listen port 51820)` を返す
- `wg show` に interface と public key が出る
- `ifconfig` に `wg0` が出る

### まだできていないこと

- Pico 2 W のオンボード Wi-Fi (`CYW43439`) を使った `wlan0` 起動(`ifup wlan0` が `-ENODEV`)
- Windows / Linux 側 WireGuard ピアとの実ハンドシェイク
- トンネル越し ping / telnet / HTTP
- 本リポジトリの `Dockerfile` への正式な `pico2w` ビルドステージ追加

Pico 2 W で ESP32-S3 と同等の実通信を行うには、次のどちらかが必要になる。

- [apache/nuttx#19250](https://github.com/apache/nuttx/pull/19250) の GSPI/CYW43439 初期化失敗(`0xffffffff`)を追う
- master にマージ済みの [apache/nuttx#19533](https://github.com/apache/nuttx/pull/19533) / `pimoroni-pico-plus-2-w:wifi` との差分を追加で確認する

---

## ESP32-WROOM-32

### 前提条件

- ESP32-WROOM-32 搭載ボード(例: 汎用 DevKitC 系)。`esptool chip_id` で正確なチップを確認できる(今回は `ESP32-D0WDQ6 rev v1.0`)
- USB ケーブル(データ通信対応のもの)
- Windows ホストの場合: Silicon Labs CP210x ドライバ(USB シリアルチップ用)、`pip install esptool`
  - Docker Desktop (Windows) は USB シリアルに直接アクセスできないため、書き込みは Windows ホスト側で `esptool` を直接実行する方式を取る(下記参照)

### ビルド(確認済み)

```bash
docker build --target esp32 -t nuttx-wireguard:esp32 .
```

`Dockerfile` の `esp32` ステージが以下を行う:

1. Xtensa ESP32 用ツールチェイン(NuttX 公式 CI 参照の Espressif prebuilt)を取得
2. `esptool`(Python)をインストール — NuttX 自身のビルド末尾の ELF→`nuttx.bin` 変換ステップに必要
3. `esp32-devkitc:wifinsh` を設定し、WireGuard 用 Kconfig(`ALLOW_BSD_COMPONENTS`・`NET_TUN`・`NET_SOCKOPTS`・`NET_WIREGUARD`・`DEV_URANDOM_ARCH`)を有効化してビルド

`wifinsh` はデフォルトで Wi-Fi 認証情報がプレースホルダー(`CONFIG_NETINIT_WAPI_SSID`/`PASSPHRASE` = `"YOUR_ROUTER_NAME"`/`"YOUR_ROUTER_PASSWORD"`)、WireGuard 秘密鍵は空。実際に使う場合はビルド前に `kconfig-tweak --set-str` で上書きする。

### 書き込み(手順は確認済み、実機成功は未確認)

```bash
# コンテナからビルド成果物を取り出す
docker create --name esp32extract nuttx-wireguard:esp32
docker cp esp32extract:/opt/nuttx/nuttx.bin ./nuttx.bin
docker rm esp32extract

# Windows ホストから直接書き込み (COM ポート番号は環境に合わせる)
python -m esptool -c esp32 -p COM5 -b 921600 write_flash -fs detect -fm dio -ff 40m 0x1000 nuttx.bin
```

このコマンドは NuttX 自身の `make flash ESPTOOL_PORT=<port> ESPTOOL_BINDIR=./` が内部で実行する `esptool` コマンドをそのまま抜き出したもの(コンテナ内で確認済み)。

**既知の問題:** 手元のボードでは何度試してもダウンロードモードに入らず(`Wrong boot mode detected (0x13)`)、書き込みまで到達できていない。`esptool` 直接・`arduino-cli` 経由のどちらでも同じ症状が出ており、ハードウェア側(自動リセット回路または BOOT ボタン)の問題を疑っている。詳細は [docs/phase4-log.md](phase4-log.md)。

### シリアルコンソール・Wi-Fi・WireGuard 確認(未実施)

書き込みが成功した後の手順:

```
nsh> wapi psk wlan0 <パスフレーズ> 3
nsh> wapi essid wlan0 <SSID> 1
nsh> ifconfig wlan0

nsh> wg
nsh> wg show
```

Linux 側ピアの設定方法は [docs/phase3-log.md](phase3-log.md) の sim/QEMU での実績と同じ(`ip link add wg0 type wireguard` による実カーネル実装を使用)。

---

## Sony Spresense + iS110B Wi-Fi Add-on(確認済み・実 Wi-Fi 疎通成功)

### 前提条件

- Spresense メインボード + Wi-Fi Add-on ボード iS110B(GS2200M)。iS110B はメインボードの拡張コネクタに直接載せる(Extension Board は不要)
- **iS110B のハードウェアバージョンを確認する**: 基板シルクの `REV:1.0` 横のドットが 無印 = v1.0A、赤 = v1.0B、黄 = v1.0C(製造元 [idy-design.com/product/is110b.html](https://idy-design.com/product/is110b.html))。GS2200M の reset/IRQ ピン配置がバージョンごとに違い、NuttX 側で `CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10{A,B,C}` から選ぶ。`Dockerfile` は手元の v1.0C に合わせてある
- Windows ホスト: Silicon Labs CP210x ドライバ(メインボードの USB シリアル)、`sonydevworld/spresense` の `sdk/tools/windows/flash_writer.exe`

### ビルド

```bash
docker build --target spresense-wifi -t nuttx-wireguard:spresense-wifi .
docker create --name x nuttx-wireguard:spresense-wifi && docker cp x:/opt/nuttx/nuttx.spk . && docker rm x

# NuttX master で作る場合(2026-09-17 時点 bda22516 で実機確認済み)
docker build --build-arg NUTTX_REF=master --target spresense-wifi -t nuttx-wireguard:spresense-wifi-master .
```

ヘッドレス起動用の Wi-Fi 資格情報は `--build-arg WIFI_SSID=... --build-arg WIFI_PASS=...` で渡す(リポジトリには入れない)。master 版は約 480 KB とやや大きく、`flash_writer` の XMODEM が 921600 bps で `Not ACK, Not NAK` になることがあるので `-b 115200` で書く。

`spresense:wifi` をベースに WireGuard 用 Kconfig を足したもの。ベース config からの変更点は `Dockerfile` の同ステージのコメントに理由込みで書いてあるが、要点は:

- `CONFIG_WL_GS2200M_DISABLE_DHCPC` を無効化(ベースは有効)。有効だとドライバが `10.0.0.2` を固定文字列で `AT+NSET` に投入し、実ネットワークと合わないまま「関連付けは成功するが疎通しない」状態になる
- `CONFIG_DEBUG_WIRELESS_ERROR` を有効化し、GS2200M の SPI 応答が期待外だったときに生バイトを出すパッチを当てている(後述の接触不良の切り分け用)

### 書き込み

```
flash_writer.exe -s -c COM6 -d -b 921600 nuttx.spk
```

メインボード単体のときと同じ。書き込み後に自動で再起動する。

### 動作確認

**注意:** メインボードの USB シリアルは DTR に反応して**接続を開くたびにボードがリセットされる**(CP210x の自動リセット回路)。pyserial 等で毎回接続を開き直すと、Wi-Fi 接続も `wg` の設定も全部飛ぶ。一連の操作は1つの開きっぱなしの接続内で行うこと。

```
nsh> gs2200m <SSID> <passphrase> &
```

`gs2200m` は接続コマンドではなく **usrsock デーモン本体**で、フォアグラウンドで動かすと NSH が戻ってこない。`&` が必須。数秒で `AT+WA` が通り、内蔵 DHCP でアドレスが付く(ログに `192.168.0.115:255.255.255.0:192.168.0.1` のように出る)。

```
nsh> wg set private-key <key>
nsh> wg set peer <Windows 側公開鍵> endpoint 192.168.0.216:51821 allowed-ips 10.11.0.1/32 persistent-keepalive 25
nsh> wg up
wg0 is up (listen port 51820)

nsh> wg show
interface: wg0
  public key: iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=
  listening port: 51820
peer: <Windows nuttx-spresense の公開鍵>
  endpoint: 192.168.0.216:51821
  latest handshake: 16 seconds ago

nsh> ifconfig
wlan0	Link encap:Ethernet HWaddr 14:5a:fc:fa:d9:6d at UP mtu 1500
	inet addr:192.168.0.115 DRaddr:192.168.0.1 Mask:255.255.255.0
wg0	Link encap:TUN at RUNNING mtu 1420
	inet addr:10.11.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0
```

Windows 側は **ESP32-S3 とは別のトンネル** `nuttx-spresense` を作る。ESP32-S3 用 `nuttx-esp32s3`(10.10.0.1/24、port 51820)と同時に有効化して、2 枚のボードを別ターミナルから同時に使うため、サブネットとリッスンポートを分けている(Spresense 側の `CONFIG_NET_WIREGUARD_LOCAL_IPADDR` も `10.11.0.2`)。`wg genkey` / `wg pubkey` で専用の鍵ペアを作り、この `.conf` を GUI で「ファイルからトンネルをインポート」→ 有効化:

```ini
[Interface]
PrivateKey = <wg genkey の出力>
ListenPort = 51821
Address = 10.11.0.1/24

[Peer]
PublicKey = iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=   # Spresense の wg show に出る公開鍵
AllowedIPs = 10.11.0.2/32
```

| | ESP32-S3 | Spresense |
|---|---|---|
| Windows 側トンネル | `nuttx-esp32s3`: 10.10.0.1/24, port 51820 | `nuttx-spresense`: 10.11.0.1/24, port 51821 |
| ボードの `wg0` | 10.10.0.2 | 10.11.0.2 |
| 接続先 | `telnet 10.10.0.2` | `telnet 10.11.0.2` |

```
> ping 10.11.0.2
Reply from 10.11.0.2: bytes=32 time=142ms TTL=128
Packets: Sent = 4, Received = 4, Lost = 0 (0% loss)
```

RTT は無負荷で 8〜9 ms(ESP32-S3 と同等)。初回の検証で 142 ms と出たのは `CONFIG_DEBUG_WIRELESS_WARN/INFO` が AT コマンド 1 本ごとに数十行ログを吐いていた負荷で、ERROR のみに絞ると解消した。

### ヘッドレス運用(電源を入れるだけで telnet 可能)— 確認済み

ESP32-S3 と同じく ROMFS の `/etc/init.d/rcS` で起動時に全部上げる。Wi-Fi の認証情報はリポジトリには入れず、ビルド引数で渡す:

```bash
docker build --target spresense-wifi   --build-arg WIFI_SSID=<ssid> --build-arg WIFI_PASS=<passphrase>   -t nuttx-wireguard:spresense-wifi .
```

rcS は `gs2200m <ssid> <pass> &` → `sleep 10` → `wg setconf /mnt/spif/wg0.conf`(あれば)→ `wg` → `denyinet on` → `telnetd &` の順([docker/spresense-etc/init.d/rcS](../../docker/spresense-etc/init.d/rcS))。鍵とピアは**初回だけ**シリアルから入れて保存する:

```
nsh> wg set private-key <key>
nsh> wg set peer <Windows 公開鍵> endpoint 192.168.0.216:51821 allowed-ips 10.11.0.1/32 persistent-keepalive 25
nsh> wg saveconf
saved to /mnt/spif/wg0.conf
```

以後は電源投入(またはシリアルの開閉によるリセット)のたびに約 15 秒で復旧する。**注意:** rcS が `denyinet on` を打った後に手で `wg down` → `wg up` すると、wg0 の UDP ソケットがカーネル側に作られて Wi-Fi に出られず、ハンドシェイクが二度と通らない。ピア設定を変えたら `wg saveconf` してリセットする(rcS が正しい順序で上げ直す)。起動ログ:

```
gs2200m [5:50]
wg0 is up (listen port 51820)
AF_INET sockets now go to the kernel stack
telnetd [10:100]
NuttShell (NSH) NuttX-12.7.0
```

シリアルを閉じた状態で Windows から `ping 10.11.0.2` → `telnet 10.11.0.2` → telnet セッション内で `webserver &` → ブラウザで `http://10.11.0.2/` まで確認済み(ESP32-S3 のデモ動画と同じ流れ)。

### デモ手順(手動)— 確認済み

rcS を使わない場合の手順。**シリアルは開きっぱなしにできるターミナル**(Tera Term、または `python -m serial.tools.miniterm COM6 115200`)を使うこと。pyserial で接続を開き直すたびに DTR でボードがリセットされる。

```
nsh> gs2200m <SSID> <passphrase> &                  # usrsock デーモン (& 必須)。DHCP まで数秒
nsh> wg set private-key <key>
nsh> wg set peer <Windows 公開鍵> endpoint 192.168.0.216:51821 allowed-ips 10.11.0.1/32 persistent-keepalive 25
nsh> wg up                                           # wg0 の UDP ソケットは GS2200M 経由で作られる
nsh> denyinet on                                     # 以後の AF_INET socket() をカーネルスタックへ
nsh> telnetd &                                       # カーネル側 TCP:23 で待ち受け (& 必須)
nsh> webserver &                                     # 同じく TCP:80
```

Windows 側:

```
> ping 10.11.0.2
> telnet 10.11.0.2                                    # NSH プロンプトが返る。uname -a / free / ifconfig など
> start http://10.11.0.2/                             # Spresense 向け文言のデモページ
```

ESP32-S3 の動画と同じく、telnet セッションの中から `webserver &` を打ってからブラウザを開いても良い(`denyinet on` は一度打てば以後有効)。

`denyinet` が必要な理由: `CONFIG_NET_USRSOCK` 環境では AF_INET の `socket()` が全部 GS2200M デーモンに行き、Wi-Fi モジュール内蔵の TCP/IP スタックで処理される。トンネル越しの接続は `wg0` で復号されて**カーネル側**の IP スタックに入るので、telnetd / webserver のリスナーもカーネル側に居ないと届かない。`denyinet on` は usrsock の `SIOCDENYINETSOCK` を叩いて、以後の `socket()` をカーネルにフォールバックさせる(既存の wg0 のソケットはそのまま)。あわせて `spresense:wifi` 既定の `CONFIG_NET_TCP_NO_STACK` / `UDP_NO_STACK` を外してカーネル側に TCP/UDP スタックを持たせている。

### 詰まった点(2026-09-16)

順に書く。どれも切り分けに時間を食ったので、同じ症状を見たら先にここを疑うこと。

1. **起動直後に `ASSERT` で落ちる、`res = ff ff ff ff ff ff ff ff`** — GS2200M からの SPI 応答が全バイト 0xFF(信号線が浮いている)。原因は iS110B のコネクタピンの曲がりによる接触不良。ピン配置 Kconfig(10A/B/C)や SPI クロックを変えても症状が1バイトも変わらないこと、Sony 系の Arduino ライブラリ(jittermaster/GS2200-WiFi、TypeC 対応版)でも同じ結果になることから、ソフトウェアではないと確定した。ピンを直して挿し直したら `res = a5 12 ...`(`0x12` = `RD_RESP_OK`)になり、その場で解決
2. **`ASSERT` の場所が分からない(`file: :0`)** — `spresense:wifi` は `CONFIG_NDEBUG=y` で、`ASSERT()` にファイル名/行番号が入らない。`CONFIG_ASSERTIONS_FILENAME` は `!NDEBUG` 依存で有効にできない。NuttX の生スタックダンプを `addr2line` にかけても、それは call trace ではなくスタック上の残骸なので信用できない(実際に誤った関数を追いかけて時間を失った)。結局はドライバに `wlerr()` を挿して bisect した
3. **`wlerr()` を挿しても何も出ない** — `wlerr` は `CONFIG_DEBUG_WIRELESS_ERROR` が無いと no-op マクロ。デフォルト無効なので、有効にするまで挿したデバッグが全部黙って消えていた
4. **Wi-Fi 関連付けは成功するのに疎通しない** — `CONFIG_WL_GS2200M_DISABLE_DHCPC=y` のとき、ドライバが `"10.0.0.2"` をハードコードで `AT+NSET` する(Kconfig の `NETINIT_IPADDR` は無関係)。無効化して内蔵 DHCP を使う
5. **`gs2200m` を実行すると NSH が戻ってこない** — デーモン本体なので `&` で起動する
6. **`wg up` は通るのに `transfer: 0 B sent` のままハンドシェイクが始まらない** — `netlib_ifup("wg0")` の `SIOCSIFFLAGS` が usrsock デーモンに横取りされ `-EINVAL` で捨てられていた(GS2200M ドライバは `ifr_name` を見ない)。同時に `SIOCSIFADDR` で GS2200M 自身の IP が `10.10.0.2` に上書きされてもいた。`wg_configure_address()` を `netdev_ifup()` 直呼びに修正して解決。詳細は [phase4-log.md](phase4-log.md)
7. **`CONFIG_CRYPTO_RANDOM_POOL` を有効にしたら `wg up` がハードフォールト(PC が ASCII 文字列)** — NuttX の `crypto/` が vendored の参照実装と同名の `blake2s_init` / `chacha20poly1305_encrypt` 等をエクスポートしていて、リンク順で別実装が呼ばれていた。`Makefile` / `CMakeLists.txt` で 18 シンボルを `-D` で `wg_` 接頭辞にリネームして解決(vendored ファイルは無変更)。`CONFIG_CRYPTO=y` の環境全般で踏むので upstream 提出前に必須の修正だった
8. **`denyinet on` が `EPERM`** — gs2200m デーモンが `SIOCDENYINETSOCK` を処理した後、ドライバにも転送して `-EINVAL` を貰い、`ioctl()` の戻り値 `-1` をそのまま返していた。デーモン側を `drvreq = false` に修正(`Dockerfile` でパッチ)
9. **`denyinet on` 後に telnetd / webserver が即死(`socket address family unsupported: 2`)** — `spresense:wifi` は `CONFIG_NET_TCP_NO_STACK=y` / `UDP_NO_STACK=y` でカーネル側に TCP/UDP スタックが無い。両方外す
10. **webserver が `/mnt` のディレクトリ一覧を返す** — httpd が `SENDFILE` 設定。`CLASSIC` + スクリプト有効に切り替えて組み込みページを出す
11. **ブラウザでページの前後に `225` / `E4` / `1D` / `0` のような数字が出る** — httpd が `HTTP/1.0` で応答しつつ `Transfer-Encoding: chunked` を付けるため、ブラウザがチャンク長をそのまま表示していた(`Invoke-WebRequest` は寛容なので気づかなかった)。`CONFIG_NETUTILS_HTTPD_ENABLE_CHUNKED_ENCODING` を両実機ステージで無効化。`Connection: close` なので本文終端は接続クローズで決まり chunked は不要

---

## 既知の未整備事項

- ESP32(無印)は実機への書き込みが完了していない(上記参照、詳細は [docs/phase4-log.md](phase4-log.md))。Spresense は解決済み
- Spresense はトンネル越し telnet / HTTP まで確認。長時間・複数ピアは未実施
- ESP32-S3 は基本的な handshake/ping 確認のみ。長時間 keepalive・再接続・複数 peer などの検証はまだ
- `CONFIG_NET_WIREGUARD_RX_STACKSIZE`(現在のデフォルト 6144)が実機の RAM 制約に対して適切かは未検証(ESP32-S3 では動作確認できたが、他ボードでの余裕は未計測)
- ピアのエンドポイント・鍵が Kconfig 固定で、実行時に変更できない([code-review-2026-08.md](code-review-2026-08.md) の課題 (D))。対向の IP が変わるとトンネルが張れず、LAN 側からの復旧が必要になる
- Raspberry Pi Pico 2 W は USB シリアル経由での NuttX 起動、`wapi`/`wg` builtin、`wg0` 起動まで確認済み。Wi-Fi は [apache/nuttx#19250](https://github.com/apache/nuttx/pull/19250) を試したが、CYW43439 の GSPI 初期化で `0xffffffff` を読み続け、`ifup wlan0` が `-ENODEV` になるため未接続(上記 Pico 2 W 節参照)
