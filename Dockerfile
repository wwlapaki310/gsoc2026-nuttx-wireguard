FROM ubuntu:24.04

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
    curl vim unzip \
  && rm -rf /var/lib/apt/lists/*

# kconfiglib: NuttX 拡張 Kconfig 構文の解析に必要 (olddefconfig 等)
RUN pip3 install --break-system-packages kconfiglib

WORKDIR /opt
RUN git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx.git nuttx && \
    git clone --depth=1 --branch nuttx-12.7.0 https://github.com/apache/nuttx-apps.git apps

# wireguard-lwip: LwIP netif ベースの WireGuard 実装 (ポーティング作業用)
RUN git clone --depth=1 https://github.com/smartalock/wireguard-lwip.git /opt/wireguard-lwip

WORKDIR /opt/nuttx
RUN ./tools/configure.sh qemu-armv7a:nsh

RUN kconfig-tweak --enable CONFIG_NET            && \
    kconfig-tweak --enable CONFIG_NET_IPv4        && \
    kconfig-tweak --enable CONFIG_NET_UDP         && \
    kconfig-tweak --enable CONFIG_VIRTIO          && \
    kconfig-tweak --enable CONFIG_VIRTIO_NET      && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_NETUTILS_PING   && \
    kconfig-tweak --enable CONFIG_MBEDTLS         && \
    kconfig-tweak --enable CONFIG_DEV_RANDOM      && \
    make olddefconfig 2>&1 | tail -5

RUN bash -c 'set -o pipefail; make -j$(nproc) 2>&1 | tail -20'

RUN file /opt/nuttx/nuttx && ls -lh /opt/nuttx/nuttx

COPY docker/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN sed -i 's/\r$//' /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

WORKDIR /workspace
EXPOSE 51820/udp

CMD ["/usr/local/bin/docker-entrypoint.sh"]
