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
