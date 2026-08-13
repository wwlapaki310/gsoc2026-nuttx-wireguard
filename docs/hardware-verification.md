# 実機検証手順 (ESP32-S3)

## スコープについて

このリポジトリで実機ターゲットとして計画されているのは **ESP32-S3 のみ**([docs/proposal.ja.md](proposal.ja.md) Phase 4)。

- **Raspberry Pi**: ロードマップに含まれていない。NuttX は Raspberry Pi Pico (RP2040) など一部の RP シリーズはサポートするが、いずれもネットワーク機能を持たないマイコンボードであり、本プロジェクトの対象外
- **SPRESENSE**: プロポーザルの自己紹介欄で「業務で使っている」と触れているだけで、開発対象ボードには含まれていない。SPRESENSE 本体には Wi-Fi が内蔵されておらず、ネットワーク接続には別売りの拡張ボードか LTE モジュールが必要なため、対応するには追加のスコープ拡張が要る

この文書は **ESP32-S3 での実機検証を実施するための手順書**。現時点でこのリポジトリには ESP32-S3 向けのビルド環境(Xtensa ツールチェイン・ボード defconfig・Wi-Fi 接続設定)がまだ用意されていないため、以下は「Phase 4 で実施する際の手順」であり、実施済みの確認結果ではない。

---

## 前提条件

### ハードウェア
- ESP32-S3 開発ボード(例: ESP32-S3-DevKitC-1)
- USB-C ケーブル(書き込み・シリアルコンソール用)
- 2.4GHz Wi-Fi アクセスポイント(ESP32-S3 と WireGuard ピア役の Linux 機が同一ネットワークに出られること)
- WireGuard ピアとなる Linux 機(`wireguard-tools` がインストール済み)

### ソフトウェア(開発機側)
- `esptool.py`(書き込み用)
- Xtensa ESP32-S3 用ツールチェイン(`xtensa-esp32s3-elf-gcc`)
  - 現在の `Dockerfile` は `arm-none-eabi` トレイン(QEMU/sim 用)のみを含んでおり、Xtensa 用ツールチェインは含まれていない。ESP32-S3 向けにビルドする場合は別途取得するか、Dockerfile に追加する必要がある
- NuttX 12.7.0 + nuttx-apps 12.7.0(本リポジトリと同じバージョン)

---

## 手順

### 1. ビルド環境の準備

```bash
git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx.git
git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx-apps.git apps

# 本リポジトリの WireGuard コンポーネントを組み込む(Dockerfile と同じ手順)
git clone --depth=1 https://github.com/smartalock/wireguard-lwip.git /tmp/wireguard-lwip
mkdir -p apps/netutils/wireguard
cp -r /tmp/wireguard-lwip/src/* apps/netutils/wireguard/
cp -r <このリポジトリ>/nuttx_port/apps/netutils/wireguard/* apps/netutils/wireguard/
cd apps/netutils && bash ../tools/mkkconfig.sh -m "Network Utilities" -o Kconfig && cd -
```

### 2. ボード設定を確認する

`nuttx/boards/xtensa/esp32s3/` 以下に実際のボードコンフィグ名(例: `esp32s3-devkit`)があるので、使用するボードに合わせて確認する:

```bash
ls nuttx/boards/xtensa/esp32s3/
```

```bash
cd nuttx
./tools/configure.sh esp32s3-devkit:nsh   # ボード名は上記確認結果に合わせる
```

### 3. Kconfig を設定する

`make menuconfig` または `kconfig-tweak` で以下を有効化する(sim/qemu で使ったものと同じ組み合わせ + ESP32 の Wi-Fi ドライバ):

```bash
kconfig-tweak --enable CONFIG_NET
kconfig-tweak --enable CONFIG_NET_IPv4
kconfig-tweak --enable CONFIG_NET_UDP
kconfig-tweak --enable CONFIG_NET_SOCKOPTS
kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS
kconfig-tweak --enable CONFIG_NET_TUN
kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500
kconfig-tweak --enable CONFIG_NET_WIREGUARD

# Wi-Fi (ESP32-S3 の実際のシンボル名は NuttX バージョン/ボード defconfig を要確認)
kconfig-tweak --enable CONFIG_ESP32S3_WIFI
kconfig-tweak --enable CONFIG_ESP32S3_WIFI_STATION
kconfig-tweak --enable CONFIG_NETUTILS_WAPI
kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG

# 乱数: ESP32-S3 はハードウェア TRNG を持つので DEV_URANDOM_ARCH が使える可能性が高い
# (sim で使った XORSHIFT128 ソフトウェア PRNG は実機では不要なはず)
kconfig-tweak --enable CONFIG_DEV_URANDOM
kconfig-tweak --enable CONFIG_DEV_URANDOM_ARCH

make olddefconfig
```

秘密鍵・ピア設定は `make menuconfig` の `Application Configuration -> Network Utilities -> WireGuard VPN support` から、または `kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PRIVATE_KEY "..."` で設定する(値は本番用に別途生成した鍵を使うこと。リポジトリのデフォルトは空)。

### 4. ビルドと書き込み

```bash
make -j$(nproc)
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash -z 0x0 nuttx.bin
# 実際のフラッシュ手順(オフセット・パーティション構成)は使用する ESP32-S3
# ボードの NuttX README (boards/xtensa/esp32s3/*/README.txt) に従うこと
```

### 5. シリアルコンソールに接続

```bash
picocom -b 115200 /dev/ttyUSB0
# または: minicom -b 115200 -D /dev/ttyUSB0
```

### 6. Wi-Fi に接続する

```
nsh> wapi psk wlan0 <パスフレーズ> 3
nsh> wapi essid wlan0 <SSID> 1
nsh> ifconfig wlan0
```

`wlan0` に有効な IP アドレス(DHCP または静的設定)が付くことを確認する。

### 7. WireGuard インターフェースを起動する

```
nsh> wg
nsh> ifconfig
```

`wg0` が `wlan0` と並んで表示されることを確認する。

### 8. Linux 側ピアを設定する

```bash
wg genkey | tee peer_private.key | wg pubkey > peer_public.key
sudo ip link add wg0 type wireguard
sudo wg set wg0 private-key peer_private.key listen-port 51820 \
  peer <ESP32-S3の公開鍵> allowed-ips 10.10.0.2/32
sudo ip addr add 10.10.0.1/24 dev wg0
sudo ip link set wg0 up
```

ESP32-S3 側の `CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP` に Linux 機の IP を設定しておくと、ESP32-S3 側からハンドシェイクを開始できる(未設定の場合は Linux 側からの開始を待ち受けるのみ)。

### 9. ハンドシェイクとトンネル疎通を確認する

```
# NuttX側:
nsh> wg show
nsh> ping 10.10.0.1

# Linux側:
$ sudo wg show
$ ping 10.10.0.2
```

`latest handshake` が両側で更新され、`ping` が通ればトンネル成立。

### 10. Flash / RAM 使用量を記録する

```bash
size nuttx
xtensa-esp32s3-elf-size nuttx
```

`.text` / `.data` / `.bss` のサイズと、実行時の空きヒープ(`nsh> free`)を記録し、[docs/proposal.ja.md](proposal.ja.md) Phase 4 の成果物としてまとめる。

---

## 既知の未整備事項

- Dockerfile に Xtensa ツールチェインが含まれていない(sim/qemu 用の `arm-none-eabi` のみ)
- ESP32-S3 用のボード defconfig 断片(Wi-Fi・WireGuard 設定込み)がリポジトリにコミットされていない
- `CONFIG_NET_TUN_PKTSIZE=1500` や RX スレッドのスタックサイズ (`CONFIG_NET_WIREGUARD_RX_STACKSIZE`, デフォルト 3072) が実機の RAM 制約に対して適切かは未検証
- `wapi` による Wi-Fi 接続を起動シーケンスに自動化する仕組み(netinit 連携)は未実装。手動での `wapi` 実行が前提
