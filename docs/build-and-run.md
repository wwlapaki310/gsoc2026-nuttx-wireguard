# WireGuard for NuttX — ビルド & 実行手順書

NuttX 上の WireGuard 実装(`apps/netutils/wireguard/`)を、各ターゲットで
ビルド・実行するための手順をまとめる。

## 対応ターゲット

| ターゲット | ボード構成 | ネットワーク | 用途 |
|---|---|---|---|
| `sim` | `sim:nsh` + NET | SIM_NETDEV (ホスト TAP) | メイン開発・動作確認 |
| `qemu` | `qemu-armv7a:nsh` | virtio-net | RTOS スケジューラ上での検証 |
| `esp32` | `esp32-devkitc:wifi` | Wi-Fi ステーション (ネイティブスタック) | 実機 1 |
| `spresense` | `spresense:rndis` | USB RNDIS (ネイティブスタック) | 実機 2 |

> **Spresense の注意**: `spresense:wifi` (gs2200m 拡張ボード) は usrsock
> ベースでソケットが Wi-Fi モジュール側に offload されるため、NuttX
> ネイティブ IP スタックを必要とする TUN (wg0) と併用できない。
> このため USB RNDIS 構成を採用している。LTE 拡張ボード (alt1250) も
> 同様に usrsock のため不可。

## アーキテクチャ概要

```
  アプリ (ping, etc.)
       │ 平文 IP パケット
  NuttX IP スタック ──── wg0 (TUN 仮想インターフェース)
       │                     │ read()/write()
       │              wireguard デーモンタスク
       │              (暗号化/復号 + ハンドシェイク + タイマ)
       │                     │ sendto()/recvfrom()
       └──── UDP ソケット (port 51820) ──── 物理 NIC ──── ピア
```

- プロトコル/暗号エンジンは [wireguard-lwip](https://github.com/smartalock/wireguard-lwip)
  の `wireguard.c` / `crypto/` を無改変で使用
- lwIP 依存の `wireguardif.c` を `nuttx-wireguardif.c`
  (TUN + BSD ソケット + poll ループ) で置き換え
- デーモンは独立タスク (`task_create`)。TUN/ソケットの fd を所有する
  (一時的な `wg` コマンドタスクが fd を持つと、コマンド終了時に OS が
  fd を閉じてインターフェースが消えるため)

## 必要な Kconfig

すべて `scripts/build.sh` / Dockerfile が自動で設定する。

```
CONFIG_NET=y
CONFIG_NET_IPv4=y
CONFIG_NET_UDP=y
CONFIG_ALLOW_BSD_COMPONENTS=y   # NET_TUN の依存
CONFIG_NET_TUN=y
CONFIG_NET_TUN_PKTSIZE=1500     # WireGuard MTU(1420) 以上必須 (デフォルト 296)
CONFIG_DEV_URANDOM=y            # 鍵生成の乱数源
CONFIG_NET_WIREGUARD=y
CONFIG_NSH_LINELEN=160          # base64 鍵入力でコマンドが 80 桁を超えるため
```

---

# 0. 前提環境 (どこで実行するか)

`scripts/build.sh` は **bash + Linux 用ツールチェーン前提**のスクリプト。
Windows の PowerShell から直接は実行できない。選択肢は次の 2 つ:

| 方法 | 向いている人 | 必要なもの |
|---|---|---|
| **A. WSL2 (Ubuntu 24.04)** | 繰り返しビルド・開発する人 (差分ビルドが速い) | WSL2 + Ubuntu-24.04 |
| **B. Docker** | 環境を汚したくない人 / PowerShell だけで済ませたい人 | Docker Desktop (WSL2 バックエンド) |

どちらも、**リポジトリを clone して 1 コマンド実行するだけ**で
NuttX 本体 / apps / wireguard-lwip の取得から成果物生成まで自動で行う。

## 0.1 A. WSL2 で実行する

PowerShell (管理者) で WSL + Ubuntu 24.04 を導入 (導入済みならスキップ):

```powershell
wsl --install -d Ubuntu-24.04
```

以降はすべて **WSL (Ubuntu) のシェル内**で実行する
(ディスク空き ~10GB、初回ビルドは 5〜15 分/ターゲット):

```bash
# 1) 依存パッケージ (下の 1.1 と同じ)
sudo apt update
sudo apt install -y git make gcc g++ unzip kconfig-frontends \
    gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
    genromfs xxd python3-pip qemu-system-arm
pip3 install --break-system-packages kconfiglib esptool

# 2) リポジトリ取得
git clone https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard.git
cd gsoc2026-nuttx-wireguard

# 3) ビルド (これだけ。ESP32 のみ 1.1 の Xtensa ツールチェーンが追加で必要)
scripts/build.sh sim
```

- `sim` の成果物 (`~/nuttx-ws/out/sim/nuttx`) は **WSL 内でそのまま実行できる**
- `qemu` も WSL 内の `qemu-system-arm` でそのまま動く
- ESP32 / Spresense への**書き込み**は「0.3 Windows からの書き込み」参照

## 0.2 B. Docker で実行する (PowerShell / WSL どちらでも同じコマンド)

前提: [Docker Desktop](https://www.docker.com/products/docker-desktop/)
をインストールし WSL2 バックエンドで起動しておく。
以下は **PowerShell でも WSL でもコマンドは同一**:

```powershell
git clone https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard.git
cd gsoc2026-nuttx-wireguard

# ビルド (ターゲットごとにイメージを作る)
docker build --target sim       -t nuttx-wg:sim .
docker build --target qemu      -t nuttx-wg:qemu .
docker build --target esp32     -t nuttx-wg:esp32 .
docker build --target spresense -t nuttx-wg:spresense .

# 実行 (sim: そのまま nsh> が起動する)
docker run --rm -it nuttx-wg:sim

# 実行 (qemu: QEMU が起動し UDP 51820 がホストに公開される)
docker run --rm -it -p 51820:51820/udp nuttx-wg:qemu

# 実機用: 成果物 (書き込みイメージ) をコンテナから取り出す
docker create --name wg-tmp nuttx-wg:esp32
docker cp wg-tmp:/opt/nuttx/nuttx.bin .
docker rm wg-tmp

docker create --name wg-tmp nuttx-wg:spresense
docker cp wg-tmp:/opt/nuttx/nuttx.spk .
docker rm wg-tmp
```

補足:
- sim で TUN/TAP (ホストとのネットワーク接続) まで試す場合は
  `docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wg:sim`
  (Linux コンテナ内で完結する分には動くが、Windows ホスト側との
  ブリッジは Docker Desktop では不可。対向テストは WSL2 の方が楽)
- コンテナ内で設定を変えて再ビルドしたい場合:
  `docker run --rm -it --entrypoint bash nuttx-wg:sim` → `cd /opt/nuttx && make menuconfig && make -j$(nproc)`

## 0.3 Windows からの実機書き込み

Docker Desktop は USB をコンテナに渡せないため、**書き込みはホスト側**で行う。

**ESP32 (PowerShell で完結する方法):**

```powershell
pip install esptool
# デバイスマネージャーで COM ポート番号を確認 (例: COM3)
esptool --chip esp32 --port COM3 --baud 921600 write_flash 0x0 nuttx.bin
```

> NuttX 12.7 の ESP32 はデフォルトで **Simple Boot** 形式:
> `nuttx.bin` 自体がブート可能イメージで、**0x0 に書くだけ**。
> 別途ブートローダーやパーティションテーブルは不要。

**Spresense (PowerShell):** `flash_writer.py` は Python スクリプトなので
Windows でも動く:

```powershell
pip install pyserial
git clone --depth=1 https://github.com/sonydevworld/spresense.git
python spresense\sdk\tools\flash_writer.py -s -c COM4 -b 115200 -n nuttx.spk
```

**WSL2 から USB を使いたい場合**: [usbipd-win](https://github.com/dorssel/usbipd-win)
で `usbipd bind` → `usbipd attach --wsl` すると WSL 内に /dev/ttyUSB0 が
現れ、Linux 用手順 (2.4 / 2.5) がそのまま使える。

---

# 1. ビルド

## 1.1 ネイティブビルド (Ubuntu 24.04)

```bash
# 依存パッケージ
sudo apt install -y kconfig-frontends gcc-arm-none-eabi binutils-arm-none-eabi \
    libnewlib-arm-none-eabi genromfs xxd python3-pip qemu-system-arm
pip3 install --break-system-packages kconfiglib esptool

# ESP32 のみ: Xtensa ツールチェーン
curl -sSL -o /tmp/xtensa.tar.xz \
  "https://dl.espressif.com/github_assets/espressif/crosstool-NG/releases/download/esp-12.2.0_20230208/xtensa-esp32-elf-12.2.0_20230208-x86_64-linux-gnu.tar.xz"
mkdir -p ~/nuttx-ws/toolchains && tar -xf /tmp/xtensa.tar.xz -C ~/nuttx-ws/toolchains

# ビルド (NuttX/apps/wireguard-lwip は初回に自動 clone される)
scripts/build.sh sim
scripts/build.sh qemu
scripts/build.sh esp32
scripts/build.sh spresense
```

成果物は `~/nuttx-ws/out/<target>/` に保存される
(ワークスペースは環境変数 `NUTTX_WS` で変更可能)。

| ターゲット | 成果物 |
|---|---|
| sim | `nuttx` (ホストで実行可能な ELF) |
| qemu | `nuttx` (ARM ELF) |
| esp32 | `nuttx.bin` |
| spresense | `nuttx.spk` |

ターゲットを切り替えると自動で `distclean` + 再 configure される。
同一ターゲットの再ビルドは差分ビルド。強制再構成は `--clean` を付ける。

## 1.2 Docker ビルド

```bash
docker build -t nuttx-wg-sim       --target sim       .
docker build -t nuttx-wg-qemu      --target qemu      .
docker build -t nuttx-wg-esp32     --target esp32     .
docker build -t nuttx-wg-spresense --target spresense .
```

---

# 2. 実行

## 2.1 wg コマンド (全ターゲット共通)

```
wg genkey                      秘密鍵を生成 (base64)
wg pubkey <private-key>        秘密鍵から公開鍵を導出
wg up -k <private-key> -a <addr> [-m <netmask>] [-p <listen-port>]
                               wg0 を作成しデーモンを起動
wg peer -P <public-key> -A <ip[/prefix]> [-e <ip:port>] [-K <keepalive-sec>]
                               ピアを追加 (-e 指定時はハンドシェイク開始)
wg status                      デバイス/ピアの状態を表示
wg down                        インターフェースを停止
```

典型的なセットアップ (対向は Linux サーバー 203.0.113.1:51820 の例):

```
nsh> wg genkey
KCsDACzp0RA26xvaHPsY2jjxW8P2+FxcEOobWTQ6xkQ=
nsh> wg pubkey KCsDACzp0RA26xvaHPsY2jjxW8P2+FxcEOobWTQ6xkQ=
iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=   ← これを対向に登録

nsh> wg up -k KCsDACzp0RA26xvaHPsY2jjxW8P2+FxcEOobWTQ6xkQ= -a 10.10.0.2
nsh> wg peer -P <対向の公開鍵> -A 10.10.0.1/32 -e 203.0.113.1:51820 -K 25
nsh> wg status
nsh> ping 10.10.0.1            ← トンネル経由の疎通確認
```

対向 Linux 側の設定例 (`/etc/wireguard/wg0.conf`):

```ini
[Interface]
PrivateKey = <Linux 側の秘密鍵>
Address    = 10.10.0.1/24
ListenPort = 51820

[Peer]
PublicKey  = iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=   # NuttX の公開鍵
AllowedIPs = 10.10.0.2/32
```

```bash
sudo wg-quick up wg0
```

## 2.2 sim (Linux プロセスシミュレータ)

```bash
# ネイティブビルドの場合
~/nuttx-ws/out/sim/nuttx

# Docker の場合 (ホストと通信するなら TUN/TAP 権限が必要)
docker run --rm -it --cap-add=NET_ADMIN --device=/dev/net/tun nuttx-wg-sim
```

sim の `eth0` はホスト側 TAP デバイスと接続される (`CONFIG_SIM_NETDEV`)。
ホスト側で TAP に IP を振れば、上記 2.1 の手順で
ホスト上の WireGuard (wg-quick) と対向させられる:

```bash
# ホスト側 (sim 起動後に tap0 が現れる)
sudo ip addr add 10.0.0.1/24 dev tap0
sudo ip link set tap0 up
# NuttX 側 eth0 は 10.0.0.2 (デフォルト)。
# あとは 2.1 の手順で wg up / wg peer -e 10.0.0.1:51820
```

終了は `poweroff` (または Ctrl-C)。

## 2.3 QEMU (ARM Cortex-A7)

```bash
# ネイティブビルドの場合
qemu-system-arm -M virt -cpu cortex-a7 -nographic -bios none \
  -kernel ~/nuttx-ws/out/qemu/nuttx \
  -net nic,model=virtio -net user,hostfwd=udp::51820-:51820

# Docker の場合
docker run --rm -it -p 51820:51820/udp nuttx-wg-qemu
```

QEMU user-mode ネットワークでは NuttX 側は 10.0.2.15、
ホスト (slirp ゲートウェイ) は 10.0.2.2 に見える。DHCP 不要 (静的設定):

```
nsh> ifconfig eth0 10.0.2.15 gw 10.0.2.2   ← gw 指定必須 (slirp の GW は 10.0.2.2)
nsh> wg up -k <秘密鍵> -a 10.10.0.2
nsh> wg peer -P <対向公開鍵> -A 10.10.0.1/32 -e 10.0.2.2:51820 -K 25
```

ホスト側で `wg-quick up` しておけば、hostfwd 経由でハンドシェイクが通る。
終了は Ctrl-A → X。

> **注意**: `gw 10.0.2.2` を指定しないと NuttX のデフォルト DRaddr
> (10.0.2.1) 宛てに ARP して応答が返せず、ハンドシェイク応答が
> 送信できない (受信だけ成功し peer が up にならない)。

## 2.4 ESP32 (ESP32-DevKitC)

書き込み (要 esptool)。NuttX 12.7 の ESP32 は Simple Boot 形式のため
`nuttx.bin` を 0x0 に書くだけでよい (ブートローダー不要):

```bash
# make から書く場合 (ポートは環境に合わせる)
cd ~/nuttx-ws/nuttx
make flash ESPTOOL_PORT=/dev/ttyUSB0 ESPTOOL_BAUD=921600

# esptool 直接の場合
esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash 0x0 ~/nuttx-ws/out/esp32/nuttx.bin
```

シリアルコンソール (115200bps) に接続して Wi-Fi 接続 → WireGuard:

```
nsh> wapi psk wlan0 <passphrase> 3
nsh> wapi essid wlan0 <SSID> 1
nsh> renew wlan0                     ← DHCP でアドレス取得
nsh> ifconfig
nsh> wg up -k <秘密鍵> -a 10.10.0.2
nsh> wg peer -P <対向公開鍵> -A 10.10.0.1/32 -e <サーバーIP>:51820 -K 25
nsh> ping 10.10.0.1
```

> メモ: `esp32-devkitc:wifi` 構成では iperf / wapi initconf を無効化して
> いる (ビルド時に外部 tarball を取得するため。WireGuard には不要)。

## 2.5 SPRESENSE

書き込み (Spresense ブートローダー導入済みの前提)。`nuttx.spk` は
NuttX ビルドが `tools/cxd56/mkspk` で自動生成する。書き込みツールは
[Spresense SDK](https://github.com/sonydevworld/spresense) 側の
`flash_writer.py` を使う:

```bash
git clone --depth=1 https://github.com/sonydevworld/spresense.git
python3 spresense/sdk/tools/flash_writer.py -s -c /dev/ttyUSB0 -b 115200 \
    -n ~/nuttx-ws/out/spresense/nuttx.spk
# または Arduino IDE / spresense-tools の書き込みツールでも可
```

USB RNDIS でホスト PC と接続 (拡張ボードの USB コネクタ):

```
# NuttX 側 (シリアルコンソール 115200bps)
nsh> ifconfig eth0 192.168.7.2      ← RNDIS インターフェース
```

```bash
# ホスト (Linux) 側: usb0 が現れる
sudo ip addr add 192.168.7.1/24 dev usb0
sudo ip link set usb0 up
```

続けて WireGuard (対向 = ホストの例):

```
nsh> wg up -k <秘密鍵> -a 10.10.0.2
nsh> wg peer -P <ホスト公開鍵> -A 10.10.0.1/32 -e 192.168.7.1:51820 -K 25
nsh> ping 10.10.0.1
```

---

# 3. 動作検証済みの E2E テスト (sim ↔ QEMU 対向)

外部の WireGuard 実装を使わずに、NuttX 同士でトンネルを検証できる。
実測で **ハンドシェイク完了 + 暗号化 ping 疎通 (3/3, RTT ~20ms)** を確認済み。

```
   sim (initiator)                         QEMU (responder)
   wg0: 10.10.0.2                          wg0: 10.10.0.1
   eth0: 10.0.0.2 ── tap0 (host 10.0.0.1) ── host:51820 ─ hostfwd ─→ eth0: 10.0.2.15
```

1. **QEMU 側 (応答側)** — hostfwd で UDP 51820 をゲストへ転送して起動:
   ```bash
   qemu-system-arm -M virt -cpu cortex-a7 -nographic -kernel out/qemu/nuttx \
     -netdev user,id=n0,hostfwd=udp::51820-:51820 -device virtio-net-device,netdev=n0
   ```
   ```
   nsh> ifconfig eth0 10.0.2.15 gw 10.0.2.2
   nsh> wg up -k <鍵A> -a 10.10.0.1
   nsh> wg peer -P <公開鍵B> -A 10.10.0.2/32 -K 0     ← endpoint なし (listen)
   ```

2. **sim 側 (起動側)** — sim を起動すると host に tap0 が現れる:
   ```bash
   sudo ip addr add 10.0.0.1/24 dev tap0 && sudo ip link set tap0 up
   ```
   ```
   nsh> ifconfig eth0 10.0.0.2
   nsh> wg up -k <鍵B> -a 10.10.0.2
   nsh> wg peer -P <公開鍵A> -A 10.10.0.1/32 -e 10.0.0.1:51820 -K 5
   nsh> wg status          ← 両側で status: up になる
   nsh> ping -c 3 10.10.0.1   ← トンネル経由で応答
   ```

---

# 4. トラブルシューティング

| 症状 | 原因 / 対処 |
|---|---|
| `wg up` が `start failed: -22` | 秘密鍵の base64 が不正 (44 文字 + `=` を確認) |
| `wg up` が `start failed: -2` | `/dev/tun` がない → `CONFIG_NET_TUN=y` を確認 |
| コマンドが途中で切れる | `CONFIG_NSH_LINELEN` が短い (160 推奨) |
| `wg status` で peer が `down` のまま | endpoint への UDP 到達性を確認。対向の `AllowedIPs` に NuttX 側トンネルアドレスが入っているか確認 |
| ping がトンネルを通らない | `wg peer -A` (allowed-ips) が対向トンネルアドレスを含むか確認 |
| ESP32 で cJSON/argtable3 のダウンロード失敗 | ネットワーク制限下では `CONFIG_NETUTILS_IPERF` / `CONFIG_WIRELESS_WAPI_INITCONF` を無効化 (build.sh は対処済み) |

## 制約事項 (現状)

- IPv4 のみ (IPv6 は未対応)
- ピア数は `WIREGUARD_MAX_PEERS` (wireguard-platform.h、デフォルト 1)
- FLAT ビルド前提 (`wg` コマンドとデーモンがグローバル状態を共有するため。
  PROTECTED/KERNEL ビルドでは IPC 化が必要)
- `wireguard_is_under_load()` は常に false (cookie 応答は受信側のみ実装)

## 乱数に関する重要な注意

`wg genkey` は `/dev/urandom` を乱数源とする。sim の
`CONFIG_DEV_URANDOM_XORSHIFT128` は**エントロピー源がなくブート毎に同じ
系列**を生成するため、sim で生成した鍵は毎回同一になる (デモ用途のみ)。
実機では以下を推奨:

- ESP32: `CONFIG_ESP32_RNG=y` (ハードウェア RNG)
- Spresense: `CONFIG_CXD56_RNG=y`
- または `CONFIG_CRYPTO_RANDOM_POOL=y` + エントロピー投入

運用鍵はホスト側 (`wg genkey`) で生成して投入するのが最も安全。
