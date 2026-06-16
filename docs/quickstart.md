# WireGuard on NuttX — クイックスタートガイド

## 必要なもの

- Docker (QEMU デモ)
- WireGuard ツール (`apt install wireguard-tools` / `brew install wireguard-tools`)

---

## 1. ビルド

```bash
# sim ターゲット (Linux プロセス、開発用)
docker build --target sim -t nuttx-wireguard:sim .

# QEMU ターゲット (ARM Cortex-A7 エミュレーション)
docker build --target qemu -t nuttx-wireguard:qemu .
```

ビルド成功確認:
```
-rwxr-xr-x 1 root root 2.1M /opt/nuttx/nuttx
```

---

## 2. QEMU デモ — NuttX ↔ ホスト WireGuard トンネル

### 2.1 ホスト側の鍵生成

```bash
# ホスト秘密鍵 / 公開鍵
wg genkey | tee host_priv.key | wg pubkey > host_pub.key

# NuttX 秘密鍵 / 公開鍵
wg genkey | tee nuttx_priv.key | wg pubkey > nuttx_pub.key

# 16進数に変換 (NuttX の wg コマンドが hex を受け付ける)
HOST_PRIV_HEX=$(cat host_priv.key | base64 -d | xxd -p -c 32)
NUTTX_PRIV_HEX=$(cat nuttx_priv.key | base64 -d | xxd -p -c 32)
HOST_PUB_HEX=$(cat host_pub.key | base64 -d | xxd -p -c 32)
NUTTX_PUB_HEX=$(cat nuttx_pub.key | base64 -d | xxd -p -c 32)
```

### 2.2 ホスト側 WireGuard 設定

```bash
# /etc/wireguard/wg-host.conf を作成
cat > /tmp/wg-host.conf <<EOF
[Interface]
PrivateKey = $(cat host_priv.key)
Address = 10.0.1.2/24
ListenPort = 51820

[Peer]
PublicKey = $(cat nuttx_pub.key)
AllowedIPs = 10.0.1.1/32
Endpoint = 127.0.0.1:51820
EOF

sudo wg-quick up /tmp/wg-host.conf
```

### 2.3 QEMU 起動

```bash
docker run --rm -it \
  -p 51820:51820/udp \
  nuttx-wireguard:qemu
```

QEMU が起動して NuttX NSH プロンプトが表示されます:
```
NuttShell (NSH) NuttX-12.7.0
nsh>
```

### 2.4 NuttX 側の WireGuard 設定

NSH プロンプトで以下を実行 (鍵は 2.1 で生成した hex 値に置き換え):

```
# ネットワーク確認
nsh> ifconfig

# WireGuard 初期化 (秘密鍵 hex + 内側 IP アドレス)
nsh> wg init <NUTTX_PRIV_HEX> 10.0.1.1/24

# ピア追加 (ホストの公開鍵、エンドポイント、許可 IP)
nsh> wg addpeer <HOST_PUB_HEX> endpoint 10.0.0.2 51820 allowedips 10.0.1.2/32

# ハンドシェイク開始
nsh> wg connect 0

# tun0 の IP 確認
nsh> ifconfig tun0
```

### 2.5 疎通確認

```bash
# ホストから NuttX (tun0 = 10.0.1.1) へ ping
ping 10.0.1.1

# NuttX から ホスト (10.0.1.2) へ ping
nsh> ping 10.0.1.2
```

---

## 3. SIM ターゲットデモ (Linux プロセス)

SIM ターゲットは TUN/TAP に `CAP_NET_ADMIN` が必要:

```bash
docker run --rm -it \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  -p 51820:51820/udp \
  nuttx-wireguard:sim
```

NuttX が起動したら QEMU と同じ手順で設定できます。

---

## 4. 実機 (ESP32-S3)

### 4.1 必要な設定

ESP32-S3 では WiFi 経由でインターネットに接続後、WireGuard トンネルを張ります。

```bash
# NuttX ソースをクローン
git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx.git
git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx-apps.git apps

# WireGuard ソース配置
git clone https://github.com/smartalock/wireguard-lwip.git /tmp/wireguard-lwip
mkdir -p apps/netutils/wireguard
cp -r /tmp/wireguard-lwip/src/* apps/netutils/wireguard/
cp -r nuttx_port/apps/netutils/wireguard/* apps/netutils/wireguard/

# ESP32-S3 設定
cd nuttx
./tools/configure.sh esp32s3-devkit:nsh

# 必要な設定を有効化
kconfig-tweak --enable CONFIG_NET
kconfig-tweak --enable CONFIG_NET_IPv4
kconfig-tweak --enable CONFIG_NET_UDP
kconfig-tweak --enable CONFIG_NET_TCP
kconfig-tweak --enable CONFIG_ESP32S3_WIFI
kconfig-tweak --enable CONFIG_NET_TUN
kconfig-tweak --enable CONFIG_MBEDTLS
kconfig-tweak --enable CONFIG_DEV_RANDOM
kconfig-tweak --enable CONFIG_NET_WIREGUARD
kconfig-tweak --enable CONFIG_NET_WIREGUARD_APP
make olddefconfig

# ビルド & フラッシュ
make -j$(nproc)
esptool.py --chip esp32s3 write_flash 0x0 nuttx.bin
```

### 4.2 ESP32-S3 NSH での設定

```
# WiFi 接続
nsh> wapi psk wlan0 <SSID> <PASSWORD>
nsh> wapi essid wlan0 <SSID> 1
nsh> renew wlan0

# WireGuard 設定 (QEMU と同じ手順)
nsh> wg init <NUTTX_PRIV_HEX> 10.0.1.1/24
nsh> wg addpeer <HOST_PUB_HEX> endpoint <HOST_PUBLIC_IP> 51820 allowedips 0.0.0.0/0
nsh> wg connect 0
nsh> ping 10.0.1.2
```

---

## トラブルシューティング

| 症状 | 原因 | 対処 |
|------|------|------|
| `open /dev/tun0 failed` | `CONFIG_TUN=y` が無効 | Dockerfile で `CONFIG_NET_TUN` を確認 |
| `wireguard_device_init failed` | 秘密鍵が不正 | hex が64文字か確認 |
| `bind port 51820 failed: EADDRINUSE` | 既に起動済み | `wg down` してから再実行 |
| handshake 後に ping が通らない | ルーティング | `ifconfig tun0` でアドレス確認 |
| ハンドシェイクが完了しない | 鍵の不一致 | NuttX 公開鍵をホスト設定と照合 |

---

## アーキテクチャメモ

```
                   ┌──────────────────────────────────┐
                   │         NuttX IP スタック         │
                   │  (eth0/eth1 via VirtIO or WiFi)  │
                   └──────────────┬───────────────────┘
                                  │ TUN driver
                   ┌──────────────┴───────────────────┐
                   │     /dev/tun0  (wg0 内側 IP)     │
                   └──────────────┬───────────────────┘
                    read()        │        write()
              ┌─────┘             │              └──────┐
              ▼                   │                     ▼
       wg_tx_thread         WireGuard            wg_rx_thread
      (plaintext out)       wireguard.c          (plaintext in)
              │             encrypt/decrypt             │
              └─────────────────┐ ┌───────────────────-┘
                                ▼ ▲
                      UDP socket :51820
                      (outer encrypted)
```
