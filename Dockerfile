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

# NuttX を取得する ref。既定は本プロジェクトが検証済みの 12.7.0 だが、
#   docker build --build-arg NUTTX_REF=master ...
# とすれば upstream master に対してビルドできる。upstream 提出は master
# ベースになるため、API のズレを早めに検出するために使う。
ARG NUTTX_REF=nuttx-12.7.0

WORKDIR /opt
RUN git clone --depth=1 --branch "${NUTTX_REF}" https://github.com/apache/nuttx.git nuttx && \
    git clone --depth=1 --branch "${NUTTX_REF}" https://github.com/apache/nuttx-apps.git apps && \
    echo "NuttX ref: ${NUTTX_REF}" > /opt/nuttx-ref.txt && \
    git -C nuttx log --oneline -1 >> /opt/nuttx-ref.txt

# apps/netutils/wireguard/ を構成。
#
# wireguard-lwip のソースは nuttx_port/ 以下に実ファイルとして取り込んである
# (元の BSD ヘッダを保持したまま、byte-identical)。ビルド時に clone しない
# 理由は3つ:
#
#   - upstream (apache/nuttx-apps) には実ファイルとしてコミットする必要が
#     あるので、このリポジトリのツリーが提出物とそのまま一致する
#   - ネットワークに依存せずビルドが再現する
#   - 上流リポジトリが動いてもビルド結果が変わらない
#
# 取り込んでいないもの: wireguardif.c/h (lwIP netif グルー。
# nuttx-wireguardif.c が置き換える) と crypto/cortex (ARM アセンブリの
# X25519。選択していない)。使わないサードパーティコードを持ち込むと
# nuttx-apps の LICENSE に列挙すべき対象が無駄に増えるため。
#
# 由来と各ファイルのライセンスは docs/license-appendix-draft.md を参照。
COPY nuttx_port/apps/netutils/wireguard/ /opt/apps/netutils/wireguard/

# NuttX master (bda22516 時点) で spresense:wifi が NSH まで到達しない回帰の
# 修正 (12.7.0 にはパターンが無いので何もしない)。
#
# sched/sched/sched_processtick.c が watchdog を wd_timer(clock_systime_ticks())
# で回すようになり、CONFIG_RTC_HIRES=y ではその値が RTC 由来になった。
# cxd56 は CONFIG_CXD56_RTC_LATEINIT で RTC を watchdog 再試行 (200 ms x 15)
# 越しに有効化するが、有効化前は clock_systime_timespec() が {0, 0} を返す
# ため watchdog が一つも満了せず、RTC を有効化する再試行タイマ自身も
# 満了しない (循環)。同時に board_power_control() の nxsched_usleep(1) も
# 絶対 tick 待ちなので永遠に寝る。RTC 未有効の間はスケジューラの tick
# カウンタから時刻を返すようにする。cxd56_rtc_initialize() はもともと
# 「RTC 有効化前の稼働時間が clock_systime_timespec() で得られる」前提で
# offset を組み立てているので、この振る舞いが本来の期待値。
# 経緯は docs/development/phase4-log.md、upstream 向けは
# docs/upstream/rtc-hires-wdog-regression-draft.md。
RUN python3 - <<'PYEOF'
path = "/opt/nuttx/sched/clock/clock_systime_timespec.c"
src = open(path).read()
old = ("  else\n"
       "    {\n"
       "      ts->tv_sec = 0;\n"
       "      ts->tv_nsec = 0;\n"
       "    }\n"
       "#elif defined(CONFIG_ALARM_ARCH) || \\\n")
new = ("  else\n"
       "    {\n"
       "      /* RTC not yet enabled: fall back to the scheduler tick counter so\n"
       "       * that watchdogs (driven by clock_systime_ticks()) keep expiring\n"
       "       * and RTC late-initialisation can complete.\n"
       "       */\n"
       "\n"
       "      clock_ticks2time(ts, clock_get_sched_ticks());\n"
       "    }\n"
       "#elif defined(CONFIG_ALARM_ARCH) || \\\n")
if old in src:
    open(path, "w").write(src.replace(old, new, 1))
    print("clock_systime_timespec.c: patched RTC_HIRES fallback before RTC enable")
else:
    print("clock_systime_timespec.c: pattern not present, nothing to patch")
PYEOF

# 同じく master の cxd56_rtc.c: up_rtc_settime() が g_rtc_lock を取ったまま
# cxd56_rtc_count() (同じロックを取る) を呼ぶ。CONFIG_SPINLOCK 無しの
# 単コアではロックが irqsave に落ちるので実害は無いが、SMP/SPINLOCK 構成
# では再帰スピンロックになる。同じコミットで追加された _nolock 版を使う。
RUN python3 - <<'PYEOF'
path = "/opt/nuttx/arch/arm/src/cxd56xx/cxd56_rtc.c"
src = open(path).read()
old = "  g_rtc_save->offset = count - cxd56_rtc_count();\n"
new = "  g_rtc_save->offset = count - cxd56_rtc_count_nolock();\n"
if "g_rtc_lock" in src and "cxd56_rtc_count_nolock" in src and old in src:
    open(path, "w").write(src.replace(old, new, 1))
    print("cxd56_rtc.c: patched up_rtc_settime() recursive g_rtc_lock")
else:
    print("cxd56_rtc.c: pattern not present, nothing to patch")
PYEOF

# netutils/Kconfig を mkkconfig.sh で再生成 (wireguard を menu に追加)
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
    kconfig-tweak --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4 && \
    kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 && \
    kconfig-tweak --set-val CONFIG_LINE_MAX 160 && \
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
    kconfig-tweak --set-val CONFIG_LINE_MAX 160 && \
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

# Wi-Fi の認証情報はビルド引数で渡す (spresense-wifi ステージと同じ流儀)。
# 渡さなければ defconfig のプレースホルダーのまま。
ARG WIFI_SSID=""
ARG WIFI_PASS=""

WORKDIR /opt/nuttx
RUN ./tools/configure.sh esp32s3-devkit:wifi && \
    if [ -n "$WIFI_SSID" ]; then \
      kconfig-tweak --set-str CONFIG_NETINIT_WAPI_SSID "$WIFI_SSID" && \
      kconfig-tweak --set-str CONFIG_NETINIT_WAPI_PASSPHRASE "$WIFI_PASS" && \
      kconfig-tweak --enable CONFIG_NETINIT_DHCPC; \
    fi && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1420 && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_ARCH && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    kconfig-tweak --enable CONFIG_NETUTILS_WEBSERVER && \
    kconfig-tweak --enable CONFIG_EXAMPLES_WEBSERVER && \
    kconfig-tweak --disable CONFIG_NETUTILS_HTTPD_ENABLE_CHUNKED_ENCODING && \
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
    kconfig-tweak --set-val CONFIG_LINE_MAX 160 && \
    kconfig-tweak --set-val CONFIG_ESP32S3_SPIFLASH_OP_TASK_STACKSIZE 3072 && \
    kconfig-tweak --set-val CONFIG_NET_WIREGUARD_MAX_PEERS 4 && \
    make olddefconfig 2>&1 | tail -5

# NOTE: SPI フラッシュ操作タスクのスタックを既定の 768 から 3072 に引き上げている。
# 実機の ps で 560/704 = 79.5%、もう一方は 80.4% で NuttX の "!" 警告が出ていた。
# 3072 なら 18.6% に下がる。"wg saveconf" がこの経路を通るため余裕を持たせておく。

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
    kconfig-tweak --enable CONFIG_CRYPTO          && \
    kconfig-tweak --enable CONFIG_CRYPTO_RANDOM_POOL && \
    kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128 && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_RANDOM_POOL && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    make olddefconfig 2>&1 | tail -5

# NOTE: CONFIG_CRYPTO_RANDOM_POOL / DEV_URANDOM_RANDOM_POOL の理由は
# spresense-wifi ステージの同じ NOTE を参照 (xorshift128 の固定シード対策)。
#
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

# =============================================================================
# spresense-wifi ステージ: spresense:wifi (実機ビルド用、Spresense + Wi-Fi
# Add-on ボード iS110B/GS2200M)。ARM Cortex-M4F。
#
# GS2200M は ESP32/ESP32-S3 の wlan0 (実 netdev) とは方式が異なり、
# NET_USRSOCK 経由の usrsock デーモンとしてソケット API をプロキシする。
# "gs2200m <ssid> <passphrase> &" でデーモンを起動する (& 必須: この
# コマンド自体がデーモン本体で、フォアグラウンドだと NSH が戻ってこない)。
# socket()/sendto() など BSD API は透過的に GS2200M 経由になる。
#
# ただし netdev 向け ioctl (SIOCSIFADDR/SIOCSIFFLAGS) も usrsock が先に
# 拾い、GS2200M ドライバは ifr_name を見ずに自分宛てとして処理するため、
# wg0 の netlib_ifup() が届かなかった。nuttx-wireguardif.c 側で netdev を
# 直接設定する形に直してある (wg_configure_address() のコメント参照)。
# 実機検証 2026-09-16: Windows 公式クライアントとの handshake、
# トンネル越し ping (RTT 8-9 ms)・telnet・HTTP (デモページ) を確認。
# =============================================================================
FROM base AS spresense-wifi

# Backport upstream fix (apache/nuttx PR #2707, merged 2021-01-18) that our
# pinned nuttx-12.7.0 checkout is missing: _read_data_len() in gs2200m.c
# issues the SPI read-header request and busy-waits up_udelay(50) *after*
# the dready() poll loop; upstream moved a shorter (30us) delay to *before*
# the loop so the module has time to react before the host starts polling.
# Upstream added it after seeing this ASSERT fire under stress testing.
# It was not the cause of the boot-time crash seen here (that was a bent
# connector pin, see below) but the pre-fix ordering is still what our
# checkout has, so the backport stays.
RUN python3 - <<'PYEOF'
import re
path = "/opt/nuttx/drivers/wireless/gs2200m.c"
src = open(path).read()
old = (
    "  _write_data(dev, hdr, sizeof(hdr));\n"
    "\n"
    "  /* Wait for data ready */\n"
    "\n"
    "  while (!dev->lower->dready(NULL))\n"
    "    {\n"
    "      /* TODO: timeout */\n"
    "    }\n"
    "\n"
    "  /* NOTE: busy wait 50us\n"
    "   * workaround to avoid an invalid frame response\n"
    "   */\n"
    "\n"
    "  up_udelay(50);\n"
    "\n"
    "  /* Read frame response */\n"
)
new = (
    "  _write_data(dev, hdr, sizeof(hdr));\n"
    "\n"
    "  /* NOTE: busy wait 30us\n"
    "   * workaround to avoid an invalid frame response\n"
    "   */\n"
    "\n"
    "  up_udelay(30);\n"
    "\n"
    "  /* Wait for data ready */\n"
    "\n"
    "  while (!dev->lower->dready(NULL))\n"
    "    {\n"
    "      /* TODO: timeout */\n"
    "    }\n"
    "\n"
    "  /* Read frame response */\n"
)
assert old in src, "upstream PR #2707 pre-fix pattern not found in gs2200m.c - already patched or source changed"
open(path, "w").write(src.replace(old, new, 1))
PYEOF

# Print the raw GS2200M SPI response bytes when this ASSERT is about to
# fire, instead of dying with a stack dump and no context (this defconfig
# sets NDEBUG, so ASSERT() carries no file/line). Hardware testing
# (2026-09-16) traced a persistent boot-time crash here to a bent pin on
# the iS110B Wi-Fi Add-on board's board-to-board connector: with the
# connector not fully seated every read comes back as 8 bytes of 0xFF (an
# idle/floating SPI line) and GPIO37 (dready) reads stuck high - confirmed
# independent of this driver by reproducing the identical symptom with
# Sony's own Arduino GS2200-WiFi library. Left in, guarded so it is silent
# in normal operation, so a reseated board can be re-tested without
# rebuilding: anything other than "ff ff ff ..." means the connector is
# seated and the module is answering.
RUN python3 - <<'PYEOF'
path = "/opt/nuttx/drivers/wireless/gs2200m.c"
src = open(path).read()
old = "  ASSERT(RD_RESP_OK == res[1]);"
new = ("  if (RD_RESP_OK != res[1])\n"
       "    {\n"
       "      wlerr(\"gs2200m bad res: %02x %02x %02x %02x %02x %02x %02x %02x\"\n"
       "            \" (n=%d)\\n\", res[0], res[1], res[2], res[3], res[4],\n"
       "            res[5], res[6], res[7], n);\n"
       "    }\n"
       "\n"
       "  ASSERT(RD_RESP_OK == res[1]);")
assert old in src, "_read_data_len ASSERT pattern not found - already patched or source changed"
src = src.replace(old, new, 1)
open(path, "w").write(src)
PYEOF

# デモ用 uIP webserver のページ (esp32s3 ステージと同じ仕組み、文言だけ
# Spresense 向け)
COPY docker/webserver-demo/header.html docker/webserver-demo/spresense/index.shtml \
     /opt/apps/examples/webserver/httpd-fs/

# "denyinet on|off" builtin。usrsock デーモンに SIOCDENYINETSOCK を送り、
# 以後の AF_INET socket() をカーネルスタックに落とす。wg0 の UDP ソケットは
# GS2200M 経由のまま、telnetd / webserver の TCP リスナーだけをカーネル側
# (= wg0 で復号したパケットが届く側) に置くために必要。中身のコメント参照。
COPY docker/spresense-denyinet/ /opt/apps/system/denyinet/

# ヘッドレス運用 (電源を入れるだけで Wi-Fi 接続 -> wg0 -> telnetd) の起動
# スクリプト。esp32s3 ステージと同じ ROMFS /etc/init.d/rcS 方式。Wi-Fi の
# 認証情報はビルド引数で渡す:
#   docker build --target spresense-wifi \
#     --build-arg WIFI_SSID=<ssid> --build-arg WIFI_PASS=<passphrase> ...
# 渡さなければ rcS の Wi-Fi 部分は #if 0 のまま (何も焼き込まれない)。
# 鍵とピアは "wg saveconf" で /mnt/spif/wg0.conf に保存しておくと rcS が
# 起動時に読む (esp32s3 と同じ)。
ARG WIFI_SSID=""
ARG WIFI_PASS=""
COPY docker/spresense-etc/init.d/rcS docker/spresense-etc/init.d/rc.sysinit \
     /opt/nuttx/boards/arm/cxd56xx/spresense/src/etc/init.d/
RUN if [ -n "$WIFI_SSID" ]; then \
      sed -i -e "s|@WIFI_SSID@|$WIFI_SSID|" -e "s|@WIFI_PASS@|$WIFI_PASS|" \
             -e 's|^#if 0 /\* WIFI_CREDENTIALS \*/$|#if 1 /* WIFI_CREDENTIALS */|' \
             /opt/nuttx/boards/arm/cxd56xx/spresense/src/etc/init.d/rcS; \
    fi && \
    printf '\nifeq ($(CONFIG_ETC_ROMFS),y)\nRCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS\nendif\n' \
      >> /opt/nuttx/boards/arm/cxd56xx/spresense/src/Make.defs

# gs2200m デーモンの SIOCDENYINETSOCK 処理は、usock_enable フラグを更新した
# あと drvreq=true のままドライバの GS2200M_IOC_IFREQ にも転送してしまう。
# ドライバ側は知らない cmd なので -EINVAL、デーモンは ioctl() の戻り値 -1 を
# そのまま result に入れて返すため、呼び出し側には EPERM に見える (フラグ
# 自体は立っている)。LTE の alt1250 デーモンと同じく、デーモン内で完結させる。
RUN python3 - <<'PYEOF'
path = "/opt/apps/wireless/gs2200m/gs2200m_main.c"
src = open(path).read()
old = ("            /* Allow to create INET socket */\n"
       "\n"
       "            priv->usock_enable = TRUE;\n"
       "          }\n"
       "        break;\n")
new = ("            /* Allow to create INET socket */\n"
       "\n"
       "            priv->usock_enable = TRUE;\n"
       "          }\n"
       "\n"
       "        /* Handled entirely here; the driver has no IFREQ for it */\n"
       "\n"
       "        ret = OK;\n"
       "        drvreq = false;\n"
       "        break;\n")
assert src.count(old) == 1, src.count(old)
open(path, "w").write(src.replace(old, new, 1))
PYEOF

WORKDIR /opt/nuttx
RUN ./tools/configure.sh spresense:wifi && \
    kconfig-tweak --enable CONFIG_NETUTILS_IFCONFIG && \
    kconfig-tweak --enable CONFIG_NET_SOCKOPTS    && \
    kconfig-tweak --enable CONFIG_ALLOW_BSD_COMPONENTS && \
    kconfig-tweak --enable CONFIG_NET_TUN         && \
    kconfig-tweak --set-val CONFIG_NET_TUN_PKTSIZE 1420 && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM     && \
    kconfig-tweak --enable CONFIG_CRYPTO          && \
    kconfig-tweak --enable CONFIG_CRYPTO_RANDOM_POOL && \
    kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128 && \
    kconfig-tweak --enable CONFIG_DEV_URANDOM_RANDOM_POOL && \
    kconfig-tweak --enable CONFIG_NET_WIREGUARD   && \
    kconfig-tweak --set-str CONFIG_NET_WIREGUARD_CONFIG_PATH "/mnt/spif/wg0.conf" && \
    kconfig-tweak --set-str CONFIG_NET_WIREGUARD_LOCAL_IPADDR "10.11.0.2" && \
    kconfig-tweak --set-str CONFIG_NET_WIREGUARD_PEER_ALLOWED_IP "10.11.0.1" && \
    kconfig-tweak --set-val CONFIG_NET_WIREGUARD_PEER_ENDPOINT_PORT 51821 && \
    kconfig-tweak --set-val CONFIG_NSH_LINELEN 160 && \
    kconfig-tweak --set-val CONFIG_LINE_MAX 160 && \
    kconfig-tweak --set-val CONFIG_SYSTEM_TELNETD_SESSION_STACKSIZE 4096 && \
    kconfig-tweak --enable CONFIG_SYSTEM_DENYINET && \
    kconfig-tweak --enable CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C && \
    kconfig-tweak --enable CONFIG_DEBUG_FEATURES && \
    kconfig-tweak --enable CONFIG_DEBUG_WIRELESS && \
    kconfig-tweak --enable CONFIG_DEBUG_WIRELESS_ERROR && \
    kconfig-tweak --enable CONFIG_DEBUG_NET && \
    kconfig-tweak --enable CONFIG_DEBUG_NET_ERROR && \
    kconfig-tweak --disable CONFIG_WL_GS2200M_DISABLE_DHCPC && \
    kconfig-tweak --disable CONFIG_NET_TCP_NO_STACK && \
    kconfig-tweak --disable CONFIG_NET_UDP_NO_STACK && \
    kconfig-tweak --disable CONFIG_NETUTILS_HTTPD_SENDFILE && \
    kconfig-tweak --disable CONFIG_NETUTILS_HTTPD_DIRLIST && \
    kconfig-tweak --enable CONFIG_NETUTILS_HTTPD_CLASSIC && \
    kconfig-tweak --disable CONFIG_NETUTILS_HTTPD_SCRIPT_DISABLE && \
    kconfig-tweak --disable CONFIG_NETUTILS_HTTPD_ENABLE_CHUNKED_ENCODING && \
    kconfig-tweak --enable CONFIG_FS_ROMFS        && \
    kconfig-tweak --enable CONFIG_ETC_ROMFS       && \
    kconfig-tweak --enable CONFIG_BOARDCTL_ROMDISK && \
    make olddefconfig 2>&1 | tail -5

# NOTE: spresense:wifi の httpd は SENDFILE (/mnt をそのまま配信) 設定。
# esp32s3 ステージと同じ組み込みページ (httpd-fs/、%!: インクルード付き) を
# 出すために CLASSIC + スクリプト有効に切り替えている。
#
# NOTE: CHUNKED_ENCODING は両実機ステージで無効。httpd は "HTTP/1.0 200 OK"
# で応答するのに "Transfer-Encoding: chunked" を付けるため、ブラウザ
# (HTTP/1.0 では chunked を解釈しない) にはチャンク長の 16 進数 ("225",
# "E4", "1D", "0") が本文として見えていた。KEEPALIVE_DISABLE=y なので
# 本文の終端は接続クローズで決まり、chunked は不要。

# NOTE: spresense:wifi は usrsock 専用構成で CONFIG_NET_TCP_NO_STACK=y /
# CONFIG_NET_UDP_NO_STACK=y (カーネル側に TCP/UDP スタックを持たず、ICMP
# だけ残している)。それでも wg0 が動くのは、wg0 の UDP ソケットが usrsock
# 経由で GS2200M にオフロードされ、復号後の ICMP はカーネルの ICMP スタック
# で返せるから。トンネル越しの telnet / HTTP はカーネル側で TCP を受ける
# 必要があるので (denyinet で socket() をカーネルに落とした上で) 両方の
# NO_STACK を外す。外さないと socket() が "address family unsupported: 2"
# (-EAFNOSUPPORT) で失敗し、telnetd / webserver が即終了する。

# NOTE: CONFIG_CRYPTO_RANDOM_POOL + CONFIG_DEV_URANDOM_RANDOM_POOL で
# /dev/urandom を割り込みタイミング由来のエントロピープールにする。既定の
# xorshift128 は devurandom_register() で定数 (w=97, x=101) をシードに
# するため、電源投入直後の最初の "wg genkey" が毎回同じ鍵になっていた
# (docs/development/phase4-log.md)。ESP32 系は DEV_URANDOM_ARCH (HW RNG)
# なので影響なし。
#
# NOTE: トンネル側アドレスは ESP32-S3 (10.10.0.2, Windows 側トンネル
# "nuttx-esp32s3" / port 51820) と衝突しないよう 10.11.0.2 にしてある。
# Windows 側は別トンネル "nuttx-spresense" (10.11.0.1/24, port 51821) を
# 立てて、両ボードを同時に別ターミナルから使えるようにする
# (docs/development/hardware-verification.md)。
#
# NOTE: CONFIG_NET_WIREGUARD_CONFIG_PATH は既定の /data/wg0.conf から
# /mnt/spif/wg0.conf に変更。spresense:wifi が SmartFS をマウントするのは
# /mnt/spif で、/data は存在しない (wg saveconf が open failed になる)。

# NOTE: spresense:wifi はデフォルトで CONFIG_NETUTILS_IFCONFIG と
# CONFIG_NET_SOCKOPTS が無効。CONFIG_NET_TUN_PKTSIZE は ESP32-S3 実機検証
# (docs/phase4-log.md) で確認済みの 1420 に合わせている (WireGuard 暗号化後
# 1452 byte になっても 1500 byte フレームに収まる値)。
#
# NOTE: Wi-Fi 認証情報 (SSID/passphrase) は Kconfig ではなく "gs2200m <ssid>
# <passphrase>" の実行時引数で渡す方式のため、esp32/esp32s3 ステージと違って
# ビルドに焼き込む値自体が存在しない。WireGuard 秘密鍵も同様にビルド後
# "wg genkey" / "wg set private-key" で実行時に投入する想定 (空がセキュアな
# デフォルト)。
#
# NOTE: 無印 spresense ステージと同じ理由で CONFIG_DEV_URANDOM は
# ARCH_HAVE_RNG を持たないボードでは software PRNG (xorshift128) にフォール
# バックする。実機検証で「起動直後の wg genkey が再起動をまたいで同じ鍵を
# 返す」ことを確認済み (未調査の既知問題)。実 Wi-Fi 経由で鍵を使う前に
# シードの与え方を確認すること。
#
# NOTE: CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C は Wi-Fi Add-on ボード
# (iS110B) の GS2200M reset/IRQ ピン配置がハードウェアバージョンごとに違う
# ことに対応するための選択 (boards/arm/cxd56xx/common/src/cxd56_gs2200m.c)。
# 手元のボード (黄色ドット = v1.0C) に合わせている。別個体を使う場合は
# シルク印字のドット色 (無印=A/赤=B/黄=C, idy-design.com/product/is110b.html
# 参照) に合わせて選び直すこと。
#
# NOTE: CONFIG_DEBUG_WIRELESS_ERROR を有効にしているのは、上の res[] ダンプ
# (wlerr) を実際に serial に出すため。wlerr は CONFIG_DEBUG_WIRELESS_ERROR
# が無いと no-op に展開され、黙って握りつぶされる。WARN/INFO まで有効に
# すると AT コマンド 1 本ごとに数十行出て console が埋まるので付けない。
#
# NOTE: spresense:wifi の既定は CONFIG_WL_GS2200M_DISABLE_DHCPC=y で、
# drivers/wireless/gs2200m.c の gs2200m_ioctl_assoc_sta() が "10.0.0.2" /
# "255.255.255.0" / "10.0.0.1" を固定文字列としてハードコードして
# AT+NSET に投入する (Kconfig 経由ではない)。実ネットワーク (192.168.0.0/24)
# と食い違い、Wi-Fi 関連付け自体は成功するのに実際には疎通しない状態に
# なっていた。CONFIG_WL_GS2200M_DISABLE_DHCPC を無効化し、内蔵 DHCP
# クライアントを使う (デフォルトの n に戻すだけ) ことで、ネットワークが
# 変わってもリビルド不要になる。

RUN make -j$(nproc) >/tmp/nuttx-build.log 2>&1 || \
    (tail -200 /tmp/nuttx-build.log && false)
RUN ls -lh /opt/nuttx/nuttx.spk

WORKDIR /workspace
