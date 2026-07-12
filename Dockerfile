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
    xxd zlib1g-dev \
    curl vim unzip xz-utils \
  && rm -rf /var/lib/apt/lists/*

# kconfiglib: NuttX 拡張 Kconfig 構文の解析に必要 (olddefconfig 等)
# esptool:   ESP32 のイメージ生成/書き込みに必要
RUN pip3 install --break-system-packages kconfiglib esptool

WORKDIR /opt
RUN git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx.git nuttx && \
    git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx-apps.git apps

# wireguard-lwip: LwIP netif ベースの WireGuard 実装 (ポーティング元)
RUN git clone --depth=1 https://github.com/smartalock/wireguard-lwip.git /opt/wireguard-lwip

# apps/netutils/wireguard/ を構成:
#   - wireguard-lwip のプロトコルコア + crypto をコピー
#   - upstream の wireguardif.[ch] は lwIP 専用のため削除
#     (NuttX では nuttx-wireguardif.c が TUN + BSD ソケットで置き換える)
#   - NuttX ポーティングファイルを上書きコピー
#   - netutils/Kconfig を mkkconfig.sh で再生成 (wireguard を menu に追加)
RUN mkdir -p /opt/apps/netutils/wireguard && \
    cp -r /opt/wireguard-lwip/src/* /opt/apps/netutils/wireguard/ && \
    rm -f /opt/apps/netutils/wireguard/wireguardif.c \
          /opt/apps/netutils/wireguard/wireguardif.h
COPY nuttx_port/apps/netutils/wireguard/ /opt/apps/netutils/wireguard/
RUN cd /opt/apps/netutils && \
    bash /opt/apps/tools/mkkconfig.sh -m "Network Utilities" -o Kconfig

# WireGuard 共通 Kconfig (全ターゲット共通)
#   NET_TUN は ALLOW_BSD_COMPONENTS に依存
#   TUN の PKTSIZE は WireGuard MTU(1420) 以上必要
RUN printf '%s\n' \
      'kconfig-tweak --enable  CONFIG_NET' \
      'kconfig-tweak --enable  CONFIG_NET_IPv4' \
      'kconfig-tweak --enable  CONFIG_NET_UDP' \
      'kconfig-tweak --enable  CONFIG_ALLOW_BSD_COMPONENTS' \
      'kconfig-tweak --enable  CONFIG_NET_TUN' \
      'kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1500' \
      'kconfig-tweak --enable  CONFIG_DEV_URANDOM' \
      'kconfig-tweak --enable  CONFIG_NET_WIREGUARD' \
      'kconfig-tweak --enable  CONFIG_NET_ICMP' \
      'kconfig-tweak --enable  CONFIG_NET_ICMP_SOCKET' \
      'kconfig-tweak --enable  CONFIG_SYSTEM_PING' \
      'kconfig-tweak --set-val CONFIG_NSH_LINELEN 160' \
      'kconfig-tweak --disable CONFIG_NETUTILS_IPERF' \
      'kconfig-tweak --disable CONFIG_SYSTEM_ARGTABLE3' \
      'kconfig-tweak --disable CONFIG_WIRELESS_WAPI_INITCONF' \
      'kconfig-tweak --disable CONFIG_NETUTILS_CJSON' \
      > /usr/local/bin/enable-wireguard.sh && \
    chmod +x /usr/local/bin/enable-wireguard.sh
# 注: iperf (argtable3) / wapi initconf (cJSON) はビルド時に外部 tarball を
#     取得するため全ターゲットで無効化 (WireGuard には不要)

# =============================================================================
# sim ステージ: sim:nsh + NET 有効化 (メイン開発環境)
# ホスト Linux プロセスとして動作。TUN/TAP 経由でネットワーク接続。
# =============================================================================
FROM base AS sim

WORKDIR /opt/nuttx
RUN ./tools/configure.sh sim:nsh && \
    bash /usr/local/bin/enable-wireguard.sh && \
    kconfig-tweak --enable CONFIG_NET_TCP && \
    kconfig-tweak --enable CONFIG_SIM_NETDEV && \
    make olddefconfig 2>&1 | tail -5

RUN make -j$(nproc)
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
# virtio-net (qemu -device virtio-net-device) を FDT 経由で検出する構成。
#   NET_LL_GUARDSIZE=32 : virtio-net のリンク層ヘッダ分の余白
#   DEV_SIMPLE_ADDRENV  : libmetal が呼ぶ up_addrenv_{pa_to_va,va_to_pa} の実装
#   DEVICE_TREE/LIBC_FDT: QEMU のデバイスツリーから virtio-mmio を検出
RUN ./tools/configure.sh qemu-armv7a:nsh && \
    bash /usr/local/bin/enable-wireguard.sh && \
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO && \
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO_MMIO && \
    kconfig-tweak --enable CONFIG_DRIVERS_VIRTIO_NET && \
    kconfig-tweak --enable CONFIG_NETDEV_LATEINIT && \
    kconfig-tweak --set-val CONFIG_NET_LL_GUARDSIZE 32 && \
    kconfig-tweak --enable CONFIG_DEV_SIMPLE_ADDRENV && \
    kconfig-tweak --enable CONFIG_DEVICE_TREE && \
    kconfig-tweak --enable CONFIG_LIBC_FDT && \
    make olddefconfig 2>&1 | tail -5

RUN make -j$(nproc)
RUN ls -lh /opt/nuttx/nuttx

COPY docker/docker-entrypoint-qemu.sh /usr/local/bin/docker-entrypoint.sh
RUN sed -i 's/\r$//' /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

WORKDIR /workspace
EXPOSE 51820/udp
CMD ["/usr/local/bin/docker-entrypoint.sh"]

# =============================================================================
# esp32 ステージ: esp32-devkitc:wifi (実機ターゲット 1)
# Wi-Fi ステーション + ネイティブ NuttX ネットワークスタック。
# 成果物: /opt/nuttx/nuttx.bin (esptool で書き込み)
# =============================================================================
FROM base AS esp32

# Xtensa ESP32 ツールチェーン (espressif ミラーから取得)
RUN curl -sSL -o /tmp/xtensa.tar.xz \
      "https://dl.espressif.com/github_assets/espressif/crosstool-NG/releases/download/esp-12.2.0_20230208/xtensa-esp32-elf-12.2.0_20230208-x86_64-linux-gnu.tar.xz" && \
    mkdir -p /opt/toolchains && \
    tar -xf /tmp/xtensa.tar.xz -C /opt/toolchains && \
    rm /tmp/xtensa.tar.xz
ENV PATH="/opt/toolchains/xtensa-esp32-elf/bin:${PATH}"

WORKDIR /opt/nuttx
RUN ./tools/configure.sh esp32-devkitc:wifi && \
    bash /usr/local/bin/enable-wireguard.sh && \
    make olddefconfig 2>&1 | tail -5

RUN make -j$(nproc)
RUN ls -lh /opt/nuttx/nuttx.bin

WORKDIR /workspace
CMD ["bash"]

# =============================================================================
# spresense ステージ: spresense:rndis (実機ターゲット 2)
# USB RNDIS + ネイティブ NuttX ネットワークスタック。
# 注: spresense:wifi (gs2200m) は usrsock ベースのため TUN と併用不可。
# 成果物: /opt/nuttx/nuttx.spk (flash_writer で書き込み)
# =============================================================================
FROM base AS spresense

WORKDIR /opt/nuttx
RUN ./tools/configure.sh spresense:rndis && \
    bash /usr/local/bin/enable-wireguard.sh && \
    make olddefconfig 2>&1 | tail -5

RUN make -j$(nproc)
RUN ls -lh /opt/nuttx/nuttx.spk

WORKDIR /workspace
CMD ["bash"]
