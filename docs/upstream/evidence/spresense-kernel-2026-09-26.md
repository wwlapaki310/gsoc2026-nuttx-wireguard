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
