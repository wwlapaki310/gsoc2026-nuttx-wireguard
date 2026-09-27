# Evidence: Spresense in-kernel WireGuard over GS2200M/usrsock (2026-09-26)

Raw, redacted capture behind the T5 Spresense row in
[verification-matrix.md](../verification-matrix.md). Scope review:
[hardware-followup-2026-09-27.md](../hardware-followup-2026-09-27.md).

## What was run

| | |
|---|---|
| Board | Spresense + iS110B (GS2200M), `CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C` |
| Image | `hw-images/spresense-kernel-wg-img.spk`, SHA-256 `aedb710ae6997b818e2d3524cbc39b392454e10921f87d166c6fa3ac0b42ea82` |
| NuttX base | `cec617dfbce984bc0bf242922c3c4d306238c4de` (tag `nuttx-13.0.1`) + the fork driver patch |
| Key config | `BUILD_FLAT=y`, `NET_WIREGUARD=y`, `SYSTEM_WG=y`, `NET_USRSOCK=y`, `IOB_NCHAINS=8`, `RTC=y` |
| Peer | Windows official WireGuard client, tunnel `nuttx-spresense` (10.11.0.1/24, port 51821), **peer config unchanged** |
| Build method | container from image `nuttx-wireguard:spresense-wifi`; removed `apps/netutils/wireguard`, applied the fork driver + IOB patches, copied `apps/system/wg`. Not yet a reproducible Dockerfile stage (see remaining-work D1/D2) |

## Serial transcript (redacted)

Wi-Fi SSID/passphrase redacted. The private key is never echoed: `wg setconf`
reads it from SmartFS and the driver does not return it.

```text
AEG
[    0.000000] cxd56_farapiinitialize: Mismatched version: loader(20585) != Self(20596)
[    0.000000] cxd56_farapiinitialize: Please update loader and gnssfw firmwares!!

NuttShell (NSH) NuttX-13.0.1
nsh> 
nsh> gs2200m <SSID> <PASSPHRASE> &
gs2200m [6:50]
nsh> 
nsh> wg setconf /mnt/spif/wg0.conf
nsh> 
nsh> wg set address 10.11.0.2/24
nsh> 
nsh> wg up
wg0 is up (listen port 51820)
nsh> 
nsh> ifconfig
wg0	Link encap:UNSPEC at RUNNING mtu 1420
	inet addr:10.11.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0

wlan0	Link encap:Ethernet HWaddr 14:5a:fc:fa:d9:6d at UP mtu 1500
	inet addr:192.168.0.115 DRaddr:192.168.0.1 Mask:255.255.255.0

nsh> 
nsh> wg show
interface: wg0
  public key: iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=
  listening port: 51820
peer: WIFidVmxoENaR+/5ExC0JjIzqnsD+JfFH0rN80+2hXI=
  endpoint: 192.168.0.216:51821
  latest handshake: 3 seconds ago
  transfer: 320 B received, 320 B sent
  persistent keepalive: every 25 seconds
nsh> 
nsh> wg show
interface: wg0
  public key: iaFmhQ2Pet5jnGn2y4UOdHB0Xu4r7q7auLVCTOKsx0A=
  listening port: 51820
peer: WIFidVmxoENaR+/5ExC0JjIzqnsD+JfFH0rN80+2hXI=
  endpoint: 192.168.0.216:51821
  latest handshake: 51 seconds ago
  transfer: 496 B received, 496 B sent
  persistent keepalive: every 25 seconds
nsh> ```

## Windows side

```text
> ping 10.11.0.2
Reply from 10.11.0.2: time=68ms
Reply from 10.11.0.2: time=5ms
Reply from 10.11.0.2: time=5ms
Reply from 10.11.0.2: time=5ms
Reply from 10.11.0.2: time=8ms
Reply from 10.11.0.2: time=7ms
Packets: Sent = 6, Received = 6, Lost = 0 (0% loss)
```

## Scope — what this does and does not show

- **Does show:** the in-kernel driver's sends egress through the real
  GS2200M/`usrsock` backend; handshake completes; 496 B carried each way;
  bidirectional ICMP through the tunnel; SmartFS config restored with the
  Windows peer unchanged (so the device public key is unchanged).
- **Does not show:** any usrsock *fault* path — blocked `psock_sendto`,
  concurrent reconfiguration during a send, stop timeout/reap, IOB exhaustion,
  or Wi-Fi loss. This is functional interoperability, not queued-output
  concurrency validation.
- **Does not show:** BUILD_KERNEL/PROTECTED isolation. This run is `BUILD_FLAT`.
- **Handshake direction was not instrumented.** The device has
  `persistent-keepalive 25` and a configured endpoint, but the capture does not
  prove which side initiated.
- **TAI64N:** this image carries the **older uptime-based** timestamp, not the
  uncommitted realtime/high-water correction. It is not evidence for #14.
- `cxd56_farapiinitialize` reports a loader/`Self` version mismatch and the
  board still boots and tunnels; that does not make the mismatch harmless for
  other features (e.g. GNSS).

## Cache-free reproducible build

Added 2026-09-27, after the early-boot cause was isolated. The 2026-09-26
image above was built on a Docker-cached tree; this recipe reproduces an
equivalent, booting image from clean checkouts, so the cache is no longer
part of the story.

```sh
git clone --depth=1 --branch nuttx-13.0.1 https://github.com/apache/nuttx.git      nx13
git clone --depth=1 --branch nuttx-13.0.1 https://github.com/apache/nuttx-apps.git apps13

# 1. the driver: git diff c95c546c..HEAD of the net-wireguard fork
#    (applies to 13.0.1 with no rejects; includes the mm/iob default)
git -C nx13 apply nuttx-driver.patch

# 2. REQUIRED for cxd56 to reach NSH: the CONFIG_RTC_HIRES fallback (#9).
#    Without it the board hangs right after cxd56_farapiinitialize.
git -C nx13 apply clock-rtchires.patch

cp -r <system-wg fork>/system/wg apps13/system/wg

cd nx13 && ./tools/configure.sh -a ../apps13 spresense:wifi
kconfig-tweak --enable  CONFIG_ALLOW_BSD_COMPONENTS
kconfig-tweak --enable  CONFIG_CRYPTO
kconfig-tweak --enable  CONFIG_CRYPTO_RANDOM_POOL
kconfig-tweak --enable  CONFIG_CRYPTO_CURVE25519
kconfig-tweak --enable  CONFIG_DEV_URANDOM
kconfig-tweak --disable CONFIG_DEV_URANDOM_XORSHIFT128
kconfig-tweak --enable  CONFIG_DEV_URANDOM_RANDOM_POOL
kconfig-tweak --enable  CONFIG_NET_WIREGUARD
kconfig-tweak --enable  CONFIG_SYSTEM_WG
kconfig-tweak --set-val CONFIG_IOB_NCHAINS 8
kconfig-tweak --set-str CONFIG_SYSTEM_WG_CONFIG_PATH "/mnt/spif/wg0.conf"
kconfig-tweak --set-val CONFIG_NSH_LINELEN 160
kconfig-tweak --set-val CONFIG_LINE_MAX 160
kconfig-tweak --enable  CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10C
kconfig-tweak --disable CONFIG_WL_GS2200M_DISABLE_DHCPC
kconfig-tweak --enable  CONFIG_DEBUG_FEATURES
kconfig-tweak --enable  CONFIG_DEBUG_WIRELESS_ERROR
make olddefconfig && make -j"$(nproc)"     # -> nuttx.spk, 430 KB
```

Result (`hw-images/spresense-kernel-wg-13-clockfix.spk`): boots to NSH and
tunnels — `ping 10.11.0.2` 6/6, 78 ms then 5-10 ms.

### Early-boot cause

The hang seen on the first fresh-clone attempts is the known cxd56
`CONFIG_RTC_HIRES` regression ([#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)),
not a property of the build cache and not WireGuard: `up_rtc_settime()` runs
before the RTC is enabled, `clock_systime_timespec()` returns `{0,0}`, so the
watchdog that would complete RTC late-initialisation never expires. The
earlier fresh tree lacked the fallback only because that patch step silently
failed to match, which is why a plain no-WireGuard control hung too.

Tested in isolation: the `cxd56_rtc.c` recursive-lock change alone does **not**
fix the hang; the `clock_systime_timespec.c` fallback does. The working image
also carries the `gs2200m.c` PR #2707 timing backport and that `cxd56_rtc.c`
change; neither was shown to be necessary for boot, and both are kept.

## Headless bring-up (2026-09-27)

`docker/spresense-kernel-etc/init.d/rcS` brings the tunnel up unattended, so
the demo is power-on only. Unlike the apps build there is no `wg` daemon to
start — the kernel driver registers `wg0` in `drivers_initialize()` — so rcS
only starts the usrsock Wi-Fi daemon, waits for DHCP, and restores the saved
configuration:

```sh
gs2200m <SSID> <PASSPHRASE> &   # substituted at build time, never committed
sleep 10
wg setconf /mnt/spif/wg0.conf
wg up
```

One-time step on a board whose configuration was written by the **older apps
image**: that file has no `Address` line (the apps build kept the tunnel
address in Kconfig), so run `wg set address 10.11.0.2/24` once and
`wg saveconf`. This build's `saveconf` writes `Address`, so from then on
`setconf` restores everything.

Verified on hardware: the board was reset over DTR with **no commands sent on
the serial line at all**, and 32 s later `ping 10.11.0.2` answered 6/6
(8/16/17/6/5/5 ms).

Image: `hw-images/spresense-kernel-headless.spk`, built by
`scripts/kernel/build-spresense-kernel.sh` from clean `nuttx-13.0.1`
checkouts (`cec617df` / `be1ae4e8`), sha256
`0b0107fd558cb5230ed102cfe852ecb2e39db8b6afb615decd01add7c65adfba`,
config sha256 `e2f8652ece24cf7acc5c657a72182b1ef48af303fee303a4fd0a6e06aa2b20c8`.

Not covered: retry/diagnostics when the AP is missing or DHCP fails, and
repeated cold-boot soak. The credentials are baked into this image, so it
must not be published.
