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
#   - NuttX ポーティングファイル (Kconfig, Make.defs, CMakeLists.txt 等) をコピー
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
    kconfig-tweak --enable CONFIG_NET               && \
    kconfig-tweak --enable CONFIG_NET_IPv4          && \
    kconfig-tweak --enable CONFIG_NET_UDP           && \
    kconfig-tweak --enable CONFIG_NET_TCP           && \
    kconfig-tweak --enable CONFIG_SIM_NETDEV        && \
    kconfig-tweak --enable CONFIG_NET_TUN           && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_NETUTILS_PING     && \
    kconfig-tweak --enable CONFIG_MBEDTLS           && \
    kconfig-tweak --enable CONFIG_DEV_RANDOM        && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD     && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD_APP && \
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
# VirtIO-NET: 外部との通信。TUN: WireGuard 内側インターフェース。
# =============================================================================
FROM base AS qemu

WORKDIR /opt/nuttx
RUN ./tools/configure.sh qemu-armv7a:nsh && \
    kconfig-tweak --enable CONFIG_NET               && \
    kconfig-tweak --enable CONFIG_NET_IPv4          && \
    kconfig-tweak --enable CONFIG_NET_UDP           && \
    kconfig-tweak --enable CONFIG_NET_TCP           && \
    kconfig-tweak --enable CONFIG_VIRTIO            && \
    kconfig-tweak --enable CONFIG_VIRTIO_NET        && \
    kconfig-tweak --enable CONFIG_NET_TUN           && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_NETUTILS_PING     && \
    kconfig-tweak --enable CONFIG_NETDEV_LATEINIT   && \
    kconfig-tweak --enable CONFIG_MBEDTLS           && \
    kconfig-tweak --enable CONFIG_DEV_RANDOM        && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD     && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD_APP && \
    make olddefconfig 2>&1 | tail -5

RUN make -j$(nproc)
RUN ls -lh /opt/nuttx/nuttx

COPY docker/docker-entrypoint-qemu.sh /usr/local/bin/docker-entrypoint.sh
RUN sed -i 's/\r$//' /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

WORKDIR /workspace
EXPOSE 51820/udp
CMD ["/usr/local/bin/docker-entrypoint.sh"]
