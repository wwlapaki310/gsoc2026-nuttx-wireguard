# 実機検証手順(ESP32-S3 / Raspberry Pi Pico 2 W / ESP32-WROOM-32 / Sony Spresense)

## 現在のステータス

**ESP32-S3 で実機検証が完全に成功した。** Wi-Fi 経由で実際のアクセスポイントに接続し、Windows 上の公式 WireGuard クライアントとの実ハンドシェイク・トンネル越し ping(0% packet loss)まで確認済み。詳細は [docs/phase4-log.md](phase4-log.md) の「ESP32-S3 — 実機での完全成功」を参照。

さらに、telnetd をトンネル越しに使う実用デモの過程で「TCP のアプリケーションデータだけがトンネルを通らない」バグ(LPWORK ワーカースレッドから `sendto()` する際に fd が `EBADF` になっていた)を発見・修正し、トンネル越し telnet セッションでのコマンド実行(`uname -a`・`uptime`・`free` など)、および `webserver &` で起動した uIP webserver へのトンネル越しブラウザアクセスまで実機で確認済み。詳細・デモ動画は [docs/phase4-log.md](phase4-log.md) と [docs/phase4-summary.md](phase4-summary.md) を参照。

ESP32-WROOM-32・Spresense は `CONFIG_NET_WIREGUARD=y` でのビルド自体はコード変更なしで成功するが、実機への書き込み・起動確認はハードウェア側の問題(ESP32: ブートモードに入らない、Spresense: USB デバイスとして列挙されない)で完了できていない。詳しい経緯は同じく [docs/phase4-log.md](phase4-log.md) を参照。

Raspberry Pi Pico 2 W は、本リポジトリが固定している NuttX 12.7.0 では RP2350/Pico 2 系のボード定義が存在しないため、そのままではビルド対象にできない。Apache NuttX master では `raspberrypi-pico-2` ボードとして USB NSH の起動が確認でき、さらに本リポジトリの WireGuard 実装を移植して `wg` builtin と `wg0` TUN インターフェースの起動まで確認した。ただし、公式 Raspberry Pi Pico 2 W 向けの Wi-Fi bringup は未整備のため、現時点で確認できたのは USB シリアル経由のローカル実行までで、Wi-Fi 経由の WireGuard ハンドシェイクは未確認。

---

## スコープについて

- **Raspberry Pi Pico / Pico 2 (無印)**: ネットワーク機能を持たないため対象外
- **Raspberry Pi Pico W / Pico 2 W**: Pico W(RP2040) と Pico 2 W(RP2350) はどちらもオンボード Wi-Fi チップとして CYW43439 を使う。NuttX master には CYW43439 系ドライバと RP2350 ボード定義が入っているが、本リポジトリが固定している NuttX 12.7.0 には Pico 2 W をそのまま使うためのボード定義が無い。また、公式 Raspberry Pi Pico 2 W ボードとしての Wi-Fi bringup は未整備のため、**現時点で確認できたのは USB シリアル経由の NuttX 起動と `wg0` 起動まで**。Wi-Fi 経由の WireGuard 通信には、公式 Pico 2 W 向けの CYW43439 bringup を追加する作業が必要
- **Sony Spresense**: 当初はプロポーザル上「著者の業務経験」として触れているのみでロードマップ外だったが、実機を保有しているため試験的に対応した。**Spresense 本体には Wi-Fi が内蔵されていない**ため、Wi-Fi 経由の WireGuard 通信には別売りの GS2200M 拡張モジュール(NuttX のボードコンフィグ `wifi`)が必要。今回はビルド・起動確認のみを目標にしている(ネットワーク到達性の検証は対象外)
- **ESP32-S3**: プロポーザル上の当初の実機ターゲット。**実機検証完了**(下記参照)

---

## ESP32-S3(確認済み・完全成功)

### 前提条件

- ESP32-S3 搭載ボード(Freenove 製、16MB フラッシュ、PSRAM 8MB 内蔵で確認)
- USB ケーブル(データ通信対応のもの)
- ESP32-S3 はチップにネイティブ USB を内蔵している(ただし今回のボードは WCH 製 CH343 ブリッジチップ経由の接続だった。それでも無印 ESP32 のようなブートモード問題は発生しなかった)

### ビルド

```bash
docker build --target esp32s3 -t nuttx-wireguard:esp32s3 .
```

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

> **注意:** ピアのエンドポイント(`CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP`)は
> 現状ビルド時固定なので、対向 PC の IP が変わるとトンネルは張れない。
> その場合は LAN 側の telnet から復旧する。実行時設定は
> [code-review-2026-08.md](code-review-2026-08.md) の課題 (D) として残っている。

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

これは `wg0` の netdev 登録、WireGuard 鍵の読み込み、TUN インターフェース起動が Pico 2 W 上でも成立することの確認である。一方で、公式 Pico 2 W ボードの Wi-Fi bringup はまだ整っていないため、ESP32-S3 のような「実 Wi-Fi + 実ピアとのハンドシェイク + ping」までは未確認。

### 前提条件

- Raspberry Pi Pico 2 W
- USB ケーブル(データ通信対応のもの)
- Windows ホストで確認
- Apache NuttX master / apps master
  - 本リポジトリの Dockerfile が固定している NuttX 12.7.0 には RP2350/Pico 2 系の `raspberrypi-pico-2` ボードが無い
  - 今回は upstream master の `boards/arm/rp23xx/raspberrypi-pico-2:usbnsh` をベースにした
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

- Pico 2 W のオンボード Wi-Fi (`CYW43439`) を使った `wlan0` 起動
- Windows / Linux 側 WireGuard ピアとの実ハンドシェイク
- トンネル越し ping / telnet / HTTP
- 本リポジトリの `Dockerfile` への正式な `pico2w` ビルドステージ追加

Pico 2 W で ESP32-S3 と同等の実通信を行うには、次のどちらかが必要になる。

- 公式 Pico 2 W 向けに CYW43439 Wi-Fi bringup を追加する
- 既に CYW43439 を使う別 RP2350 ボード設定を起点に、公式 Pico 2 W のピン配置・電源制御・ファームロード差分を吸収する

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

## Sony Spresense(メインボード単体)

### 前提条件

- Spresense メインボード単体(拡張ボードなしで動作確認)
- Windows ホストの場合: `sonydevworld/spresense` リポジトリの `sdk/tools/windows/` から `flash_writer.exe`・`xmodem_writer.exe`・`cxd5602cdc-usb-driver.zip` を取得

### ビルド(確認済み)

```bash
docker build --target spresense -t nuttx-wireguard:spresense .
```

`Dockerfile` の `spresense` ステージは既存の `arm-none-eabi-gcc`(Cortex-M4F 対応)をそのまま使い、新規ツールチェインは不要。`spresense:nsh` はデフォルトで `CONFIG_NET` と `CONFIG_SCHED_WORKQUEUE` が無効なため、これらを明示的に有効化した上で WireGuard 用 Kconfig を追加している。ビルドに成功すると `tools/cxd56/mkspk` が自動的に呼ばれ、書き込み用の `nuttx.spk` が生成される。

### 書き込み(未実施 — ボードが USB デバイスとして認識されていない)

```bash
docker create --name spresenseextract nuttx-wireguard:spresense
docker cp spresenseextract:/opt/nuttx/nuttx.spk ./nuttx.spk
docker rm spresenseextract

# Windows ホストから (COM ポート番号は環境に合わせる)
flash_writer.exe -c COM5 -d nuttx.spk
```

**既知の問題:** 手元のボードが Windows 上で USB デバイスとして一切列挙されない(電源 LED は点灯するが、COM ポートはおろか「不明なデバイス」としてすら現れない)。ケーブル・USB ポートを変えても変化なし。CDC ドライバのインストールは、そもそも列挙されない状態のため未実施。詳細は [docs/phase4-log.md](phase4-log.md)。

---

## 既知の未整備事項

- ESP32(無印)・Spresense とも実機への書き込み・起動確認が完了していない(上記参照、詳細は [docs/phase4-log.md](phase4-log.md))
- ESP32-S3 は基本的な handshake/ping 確認のみ。長時間 keepalive・再接続・複数 peer などの検証はまだ
- `CONFIG_NET_WIREGUARD_RX_STACKSIZE`(デフォルト 3072)が実機の RAM 制約に対して適切かは未検証(ESP32-S3 では動作確認できたが、他ボードでの余裕は未計測)
- ピアのエンドポイント・鍵が Kconfig 固定で、実行時に変更できない([code-review-2026-08.md](code-review-2026-08.md) の課題 (D))。対向の IP が変わるとトンネルが張れず、LAN 側からの復旧が必要になる
- Raspberry Pi Pico 2 W は USB シリアル経由での NuttX 起動と `wg0` 起動まで確認済み。ただし、公式 Pico 2 W 向け Wi-Fi bringup が未整備のため、Wi-Fi 経由の WireGuard 実通信は未確認(上記 Pico 2 W 節参照)
