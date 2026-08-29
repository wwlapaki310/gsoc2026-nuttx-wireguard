FROM ubuntu:24.04 AS base

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Tokyo

RUN apt-get update -q && apt-get install -y --no-install-recommends \
    git cmake ninja-build make \
    gcc g++ \
    gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
    qemu-system-arm \
    kconfig-frontends \
    python3 python3-pip python3-pyelftools \
    iproute2 iputils-ping \
    genromfs \
    patch \
    xxd zlib1g-dev \
    curl vim unzip \
  && rm -rf /var/lib/apt/lists/*

# kconfiglib: NuttX 拡張 Kconfig 構文の解析に必要 (olddefconfig 等)
RUN pip3 install --break-system-packages kconfiglib

WORKDIR /opt
RUN git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx.git nuttx && \
    git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx-apps.git apps

# wireguard-lwip: LwIP netif ベースの WireGuard 実装 (ポーティング作業用)
RUN git clone --depth=1 https://github.com/smartalock/wireguard-lwip.git /opt/wireguard-lwip

# apps/netutils/wireguard/ を構成:
#   - wireguard-lwip のソースをコピー
#   - NuttX ポーティングファイル (Kconfig, Make.defs, CMakeLists.txt, nuttx-platform.c) をコピー
#   - netutils/Kconfig を mkkconfig.sh で再生成 (wireguard を menu に追加)
RUN mkdir -p /opt/apps/netutils/wireguard && \
    cp -r /opt/wireguard-lwip/src/* /opt/apps/netutils/wireguard/
COPY nuttx_port/apps/netutils/wireguard/ /opt/apps/netutils/wireguard/
RUN cd /opt/apps/netutils && \
    bash /opt/apps/tools/mkkconfig.sh -m "Network Utilities" -o Kconfig

# =============================================================================
# sim ステージ: sim:nsh + NET 有効化 (メイン開発環境)
# ホスト Linux プロセスとして動作。TUN/TAP 経由でネットワーク接続。
# =============================================================================
FROM base AS sim

WORKDIR /opt/nuttx
RUN ./tools/configure.sh sim:nsh && \
    kconfig-tweak --enable CONFIG_NET             && \
    kconfig-tweak --enable CONFIG_NET_IPv4        && \
    kconfig-tweak --enable CONFIG_NET_UDP         && \
    kconfig-tweak --enable CONFIG_NET_TCP         && \
    kconfig-tweak --enable CONFIG_SIM_NETDEV      && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_NETUTILS_PING   && \
    kconfig-tweak --enable CONFIG_MBEDTLS         && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500 && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_XORSHIFT128 && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 && \
    make olddefconfig 2>&1 | tail -5

# NOTE: CONFIG_NET_TUN (needed for the NET_LL_TUN link type wg0 registers
# as) depends on CONFIG_ALLOW_BSD_COMPONENTS - both tun.c and the vendored
# wireguard-lwip sources are BSD-3-Clause licensed. Without it, olddefconfig
# silently drops CONFIG_NET_TUN and, transitively, CONFIG_NET_WIREGUARD.
#
# NOTE: CONFIG_DEV_RANDOM (a hardware TRNG /dev/random) does not work on
# sim: it depends on ARCH_HAVE_RNG, which the sim architecture does not
# select, so enabling it here would be silently dropped by olddefconfig.
# wireguard_random_bytes() needs /dev/urandom, which CONFIG_DEV_URANDOM
# provides via a software PRNG (xorshift128) with no such dependency.

RUN make -j$(nproc) >/tmp/nuttx-build.log 2>&1 || \
    (tail -200 /tmp/nuttx-build.log && false)
RUN ls -lh /opt/nuttx/nuttx

COPY docker/docker-entrypoint-sim.sh /usr/local/bin/docker-entrypoint.sh
RUN sed -i 's/\r$//' /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

WORKDIR /workspace
EXPOSE 51820/udp
CMD ["/usr/local/bin/docker-entrypoint.sh"]

# =============================================================================
# qemu ステージ: qemu-armv7a:nsh (RTOS 動作検証環境)
# ARM Cortex-A7 エミュレーション。NuttX 自身のスケジューラで動作。
# =============================================================================
FROM base AS qemu

WORKDIR /opt/nuttx
RUN ./tools/configure.sh qemu-armv7a:nsh && \
    kconfig-tweak --enable CONFIG_NET             && \
    kconfig-tweak --enable CONFIG_NET_IPv4        && \
    kconfig-tweak --enable CONFIG_NET_UDP         && \
    kconfig-tweak --set-val CONFIG_NET_LL_GUARDSIZE 32 && \
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO  && \
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO_MMIO && \
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO_NET && \
    kconfig-tweak --enable CONFIG_DEVICE_TREE     && \
    kconfig-tweak --enable CONFIG_LIBC_FDT        && \
    kconfig-tweak --enable CONFIG_DEV_SIMPLE_ADDRENV && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_NETUTILS_PING   && \
    kconfig-tweak --enable CONFIG_NETDEV_LATEINIT && \
    kconfig-tweak --enable CONFIG_MBEDTLS         && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500 && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_XORSHIFT128 && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 && \
    make olddefconfig 2>&1 | tail -5

RUN make -j$(nproc) >/tmp/nuttx-build.log 2>&1 || \
    (tail -200 /tmp/nuttx-build.log && false)
RUN ls -lh /opt/nuttx/nuttx

COPY docker/docker-entrypoint-qemu.sh /usr/local/bin/docker-entrypoint.sh
RUN sed -i 's/\r$//' /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

WORKDIR /workspace
EXPOSE 51820/udp
CMD ["/usr/local/bin/docker-entrypoint.sh"]

# =============================================================================
# esp32 ステージ: esp32-devkitc:wifinsh (実機ビルド用、ESP32-WROOM-32)
# Xtensa LX6。書き込みは Windows ホスト側の esptool から行う想定
# (Docker Desktop on Windows は USB シリアルに直接アクセスできないため、
#  `docker cp` で nuttx.bin を取り出してホストで esptool を実行する)。
# 詳細・実機での書き込み結果は docs/hardware-verification.md を参照。
# =============================================================================
FROM base AS esp32

RUN apt-get update -q && apt-get install -y --no-install-recommends xz-utils \
  && rm -rf /var/lib/apt/lists/*

# Xtensa ESP32 用ツールチェイン (NuttX 公式 CI が参照している prebuilt バイナリ)
RUN mkdir -p /opt/xtensa-esp32-elf && \
    curl -s -L "https://github.com/espressif/crosstool-NG/releases/download/esp-12.2.0_20230208/xtensa-esp32-elf-12.2.0_20230208-x86_64-linux-gnu.tar.xz" \
    | tar -C /opt/xtensa-esp32-elf --strip-components 1 -xJ
ENV PATH="/opt/xtensa-esp32-elf/bin:${PATH}"

# esptool (Python) は NuttX 自身のビルド末尾 "MKIMAGE: ESP32 binary" ステップ
# (ELF -> nuttx.bin 変換) に必要。実機書き込み自体はホスト側で行うため使わない。
RUN pip3 install --break-system-packages esptool

WORKDIR /opt/nuttx
RUN ./tools/configure.sh esp32-devkitc:wifinsh && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500 && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_ARCH && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    make olddefconfig 2>&1 | tail -5

# NOTE: esp32-devkitc:wifinsh はデフォルトでプレースホルダーの Wi-Fi 認証情報
# (CONFIG_NETINIT_WAPI_SSID/PASSPHRASE = "YOUR_ROUTER_NAME"/"YOUR_ROUTER_PASSWORD")
# と空の CONFIG_NET_WIREGUARD_PRIVATE_KEY を持つ。実際に使うにはビルド前に
# kconfig-tweak --set-str で両方とも実際の値に上書きする必要がある。
#
# NOTE: CONFIG_DEV_URANDOM_ARCH は ESP32 の実ハードウェア RNG を使う
# (sim で使ったソフトウェア PRNG の XORSHIFT128 とは異なる。docs/phase2-log.md 参照)。

RUN make -j$(nproc) >/tmp/nuttx-build.log 2>&1 || \
    (tail -200 /tmp/nuttx-build.log && false)
RUN ls -lh /opt/nuttx/nuttx.bin

WORKDIR /workspace

# =============================================================================
# esp32s3 ステージ: esp32s3-devkit:wifi (実機ビルド用、ESP32-S3)
# Xtensa LX7。無印 ESP32 と異なりチップにネイティブ USB を内蔵しており、
# CP210x のような外付け USB シリアル変換チップ・自動リセット回路に依存しない
# (docs/phase4-log.md で無印 ESP32 の書き込みに使ったのと同じ問題を避けられる
#  可能性が高い、という見立て)。書き込み手順は esp32 ステージと同様、
# Windows ホスト側の esptool から行う。
# =============================================================================
FROM base AS esp32s3

RUN apt-get update -q && apt-get install -y --no-install-recommends xz-utils \
  && rm -rf /var/lib/apt/lists/*

# Xtensa ESP32-S3 用ツールチェイン (NuttX 公式 CI が参照している prebuilt バイナリ)
RUN mkdir -p /opt/xtensa-esp32s3-elf && \
    curl -s -L "https://github.com/espressif/crosstool-NG/releases/download/esp-12.2.0_20230208/xtensa-esp32s3-elf-12.2.0_20230208-x86_64-linux-gnu.tar.xz" \
    | tar -C /opt/xtensa-esp32s3-elf --strip-components 1 -xJ
ENV PATH="/opt/xtensa-esp32s3-elf/bin:${PATH}"

RUN pip3 install --break-system-packages esptool

# デモ用 uIP webserver のページを差し替え (docs/phase4-log.md の
# 「トンネル越し telnet で見つかった TCP 特有バグ」節の実演で使用)
COPY docker/webserver-demo/header.html docker/webserver-demo/index.shtml \
     /opt/apps/examples/webserver/httpd-fs/

# ヘッドレス運用 (USB シリアルなし) のための起動スクリプト。
# apps/nshlib/nsh_init.c は netinit_bringup() の後に /etc/init.d/rcS を
# 実行するので、ここで wg を叩けば電源投入だけでトンネルが上がる。
# ROMFS への焼き込みは boards/Board.mk の RCSRCS 経由で行われるため、
# ボードの src/Make.defs にその指定を追加する (esp32s3-devkit には
# 元々 etc/ が無い。esp32c3-devkit の同等の記述に倣った)。
COPY docker/esp32s3-etc/init.d/rcS docker/esp32s3-etc/init.d/rc.sysinit \
     /opt/nuttx/boards/xtensa/esp32s3/esp32s3-devkit/src/etc/init.d/
RUN printf '\nifeq ($(CONFIG_ETC_ROMFS),y)\nRCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS\nendif\n' \
    >> /opt/nuttx/boards/xtensa/esp32s3/esp32s3-devkit/src/Make.defs

WORKDIR /opt/nuttx
RUN ./tools/configure.sh esp32s3-devkit:wifi && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1420 && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_ARCH && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    kconfig-tweak --enable CONFIG_NETUTILS_WEBSERVER && \
    kconfig-tweak --enable CONFIG_EXAMPLES_WEBSERVER && \
    kconfig-tweak --enable CONFIG_FS_ROMFS        && \
    kconfig-tweak --enable CONFIG_ETC_ROMFS       && \
    kconfig-tweak --enable CONFIG_BOARDCTL_ROMDISK && \
    kconfig-tweak --enable CONFIG_NETUTILS_TELNETD && \
    make olddefconfig >/dev/null 2>&1 && \
    kconfig-tweak --enable CONFIG_SYSTEM_TELNETD  && \
    make olddefconfig >/dev/null 2>&1 && \
    kconfig-tweak --enable CONFIG_NSH_TELNET      && \
    kconfig-tweak --set-val CONFIG_SYSTEM_TELNETD_SESSION_STACKSIZE 4096 && \
    kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 && \
    make olddefconfig 2>&1 | tail -5

# NOTE: telnetd の3つの Kconfig は依存が段階的 (NETUTILS_TELNETD →
# SYSTEM_TELNETD → NSH_TELNET) なので、間に olddefconfig を挟まないと
# 後段が黙って落ちる。NSH_TELNET が入ると nsh_init.c が nsh_telnetstart()
# を呼び、ポート 23 の telnetd が起動時に自動で上がる。

# NOTE: CONFIG_NSH_LINELEN を既定の 64 から 160 に引き上げている。
# "wg set peer <44 文字の base64 鍵> endpoint <ip:port> allowed-ips <cidr>
#  persistent-keepalive <n>" は約 134 文字あり、64 では途中で切られて
# 残りが別のコマンドとして解釈される (実機で踏んだ)。実行時設定を使うなら
# 必須の設定。

# NOTE: telnet セッションのスタックを既定の 3072 から 4096 に引き上げている。
# 実機の ps で 2448/3072 = 81.1% まで積まれており、NuttX が 80% 超で付ける
# "!" 警告が出ていた (トンネル越しに NSH コマンドを実行した状態で計測)。
# NSH のコマンド実行はセッションタスクのスタック上で起きるため、コマンド次第で
# さらに深くなりうる。wg_rx 側 (CONFIG_NET_WIREGUARD_RX_STACKSIZE) と同じく、
# 余裕を実測に基づいて確保しておく。

# NOTE: NET_TUN_PKTSIZE is wg0's MTU. 1420 (not the 1500 the sim/qemu
# stages use) matches WIREGUARDIF_MTU in the vendored wireguard-lwip code
# and is what the ESP32-S3 hardware testing actually ran with: a 1420 byte
# plaintext encrypts to a 1452 byte UDP payload, which still fits in one
# 1500 byte Ethernet frame. At 1500 every full-size packet would encrypt
# into something that has to be IP-fragmented on the way out.

# NOTE: esp32s3-devkit:wifi もプレースホルダーの Wi-Fi 認証情報・空の
# WireGuard 秘密鍵を持つ。実際に使うにはビルド前に kconfig-tweak --set-str
# で上書きする(esp32 ステージと同じ、docs/hardware-verification.md 参照)。

RUN make -j$(nproc) >/tmp/nuttx-build.log 2>&1 || \
    (tail -200 /tmp/nuttx-build.log && false)
RUN ls -lh /opt/nuttx/nuttx.bin

WORKDIR /workspace

# =============================================================================
# spresense ステージ: spresense:nsh (実機ビルド用、Sony Spresense メインボード)
# ARM Cortex-M4F。base で既に用意済みの arm-none-eabi-gcc をそのまま使う。
# 書き込みには Sony 提供の flash_writer (NuttX リポジトリには同梱されておらず、
# sonydevworld/spresense の sdk/tools/windows/flash_writer.exe を別途取得する
# 必要がある) が要る。詳細は docs/hardware-verification.md を参照。
# =============================================================================
FROM base AS spresense

WORKDIR /opt/nuttx
RUN ./tools/configure.sh spresense:nsh && \
    kconfig-tweak --enable CONFIG_NET             && \
    kconfig-tweak --enable CONFIG_NET_IPv4        && \
    kconfig-tweak --enable CONFIG_NET_UDP         && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_SCHED_WORKQUEUE && \
    kconfig-tweak --enable CONFIG_SCHED_HPWORK    && \
    kconfig-tweak --enable CONFIG_SCHED_LPWORK    && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500 && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    make olddefconfig 2>&1 | tail -5

# NOTE: spresense:nsh は最小構成の NSH config で、デフォルトではネットワーク
# (CONFIG_NET) 自体が無効。CONFIG_SCHED_WORKQUEUE も無効 (sim/qemu では
# デフォルトで有効だったため気づかなかった依存関係。drivers/net/tun.c が
# 要求する。docs/phase2-log.md 参照) なので、両方を明示的に有効化している。
#
# Spresense にはWi-Fiが内蔵されていないため、このコンフィグは
# 「CONFIG_NET_WIREGUARD がビルドできて wg0 を登録できる」ことの確認用であり、
# 実際のネットワーク到達性は検証できない。Wi-Fi には別売りの GS2200M
# 拡張モジュール (ボードコンフィグ "wifi") が必要でスコープ外。

RUN make -j$(nproc) >/tmp/nuttx-build.log 2>&1 || \
    (tail -200 /tmp/nuttx-build.log && false)
RUN ls -lh /opt/nuttx/nuttx.spk

WORKDIR /workspace
