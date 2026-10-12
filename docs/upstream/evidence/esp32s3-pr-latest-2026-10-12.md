# ESP32-S3 hardware run of the submitted series (2026-10-12)

What this shows: the split series that is prepared for upstream runs on
real hardware, in a normal FLAT build, with the official `wg` command.

| | |
|---|---|
| nuttx | `237b6e538eb8e11a05a044105dc2507800802862` (`pr-latest/doc-system-wg`: PR-C + PR-K1 + PR-D on upstream `4bc1ab2fd7`) |
| nuttx-apps | `fc640afc4c9197972fca68495110429a6b8702a2` (`pr-latest/system-wg`, PR-A on upstream `74df79a53`) |
| board | M5Stack CoreS3 (ESP32-S3) in the StackChan body, built as `esp32s3-devkit` |
| base config | `esp32s3-devkit:wifi` |
| network | phone hotspot; peer is Linux kernel WireGuard in a Docker container on the laptop |
| build | FLAT, no code changes; config changes only (below) |

Config changes on top of `esp32s3-devkit:wifi` (kconfig-tweak):

- console on the CoreS3 USB-Serial/JTAG: `ESP32S3_UART0=n`, `UART0_SERIAL_CONSOLE=n`, `ESP32S3_USBSERIAL=y`
- driver and command as in `sim:wireguard`: `ALLOW_BSD_COMPONENTS`, `CRYPTO`, `CRYPTO_RANDOM_POOL`,
  `DEV_URANDOM`, `DEV_URANDOM_RANDOM_POOL`, `NET_WIREGUARD`, `NET_WIREGUARD_MAX_PEERS=4`, `SYSTEM_WG`
- test tools: `NETUTILS_DHCPC`, `NETINIT_DHCPC`, `SYSTEM_PING`
- `NSH_LINELEN=160`, `LINE_MAX=160`: the default 64 cuts `wg set peer <key> allowed-ips ...`

Build: compiler warnings 0. Image 785.8 KB.

## Result

- `wg0` registers at boot, `wg_rx` runs, `wg up` brings it up on port 51820
- Handshake with the Linux peer completes
- Board to peer: `ping -c 10 10.10.0.1`, 10/10, avg 13 ms
- Peer to board: 20/20 (56 B), 20/20 (1000 B), 5/5 (1372 B with DF, the full 1420 MTU), avg 15-17 ms
- The first peer-to-board pings (0/20 and 0/20 in the log) ran while the peer still held the
  session keys from before the board was reflashed; the board drops those as expected. They pass
  after the board's own ping starts a new handshake.

Not covered here: BUILD_PROTECTED and BUILD_KERNEL on this board, long runs, power-cycle recovery,
TCP. Keys are shown as `***`; the hotspot SSID and passphrase are not in the log.

## Log

```
nsh> 
nsh>
nsh> uname -a
uname -a
NuttX  0.0.0  Oct 12 2026 13:50:34 xtensa esp32s3-devkit
nsh>
nsh> ifconfig wlan0
ifconfig wlan0
wlan0	Link encap:Ethernet HWaddr 7c:4f:ad:af:cf:0c at RUNNING mtu 1500
	inet addr:10.252.156.2 DRaddr:10.252.156.151 Mask:255.255.255.0

nsh>
nsh> wg down
wg down
nsh>
nsh> wg set private-key ***
wg set private-key ***
nsh>
nsh> wg set listen-port 51820
wg set listen-port 51820
nsh>
nsh> wg set address 10.10.0.2/24
wg set address 10.10.0.2/24
nsh>
nsh> wg set peer *** allowed-ips 10.10.0.1/32
wg set peer *** allowed-ips 10.10.0.1/32
nsh>
nsh> wg up
wg up
wg0 is up (listen port 51820)
nsh>
nsh> ifconfig wg0
ifconfig wg0
wg0	Link encap:UNSPEC at UP mtu 1420
	inet addr:10.10.0.2 DRaddr:0.0.0.0 Mask:255.255.255.0

nsh>
--- 10.10.0.2 ping statistics ---
20 packets transmitted, 0 received, 100% packet loss, time 6079ms
20 packets transmitted, 0 received, 100% packet loss, time 6081ms
nsh> ping -c 10 10.10.0.1
ping -c 10 10.10.0.1
PING 10.10.0.1 56 bytes of data
56 bytes from 10.10.0.1: icmp_seq=0 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=1 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=2 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=3 time=30.0 ms
56 bytes from 10.10.0.1: icmp_seq=4 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=5 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=6 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=7 time=20.0 ms
56 bytes from 10.10.0.1: icmp_seq=8 time=10.0 ms
56 bytes from 10.10.0.1: icmp_seq=9 time=10.0 ms
10 packets transmitted, 10 received, 0% packet loss, time 10110 ms
rtt min/avg/max/mdev = 10.000/13.000/30.000/6.403 ms
nsh>
nsh> wg show
wg show
interface: wg0
  public key: ***
  listening port: 51820
peer: ***
  endpoint: 10.252.156.89:63799
  latest handshake: 15 seconds ago
  transfer: 1136 B received, 1120 B sent
nsh>
endpoint: 10.252.156.2:51820
  latest handshake: 19 seconds ago
  transfer: 2.24 KiB received, 52.29 KiB sent
nsh> free
free
      total       used       free    maxused    maxfree  nused  nfree name
     272216      81224     190992      85648     185888    181      9 Umem
nsh>
nsh> ps
ps
  TID   PID  PPID PRI POLICY   TYPE    NPX STATE    EVENT     SIGMASK            STACK COMMAND
    0     0    18   0 FIFO     Kthread   - Ready              0000000000000000 0003040 Idle_Task
    1     0    18 100 RR       Kthread   - Waiting  Semaphore 0000000000000000 0001960 lpwork 0x3fca87a0 0x3fca87f8
    3     0    18 252 RR       Kthread   - Waiting  Semaphore 0000000000000000 0004520 esp_timer 0x3fcae3f0
    4     0    18 253 RR       Kthread   - Waiting  MQ empty  0000000000000000 0006600 wifi
    5     5     0 100 RR       Task      - Waiting  Semaphore 0000000000000000 0008120 init_main
    6     6     5 100 RR       Task      - Running            0000000000000000 0008144 sh
    7     0    18 100 RR       Kthread   - Waiting  Semaphore 0000000000020000 0001960 netdev-wlan0 0x3fcb8b10 0
   24     0    18 100 RR       Kthread   - Waiting  Semaphore 0000000000000000 0006072 wg_rx 0
nsh>
== PC side after the board-initiated handshake
20 packets transmitted, 20 received, 0% packet loss, time 5718ms
rtt min/avg/max/mdev = 11.588/15.241/17.672/1.797 ms
20 packets transmitted, 20 received, 0% packet loss, time 5717ms
rtt min/avg/max/mdev = 13.040/16.841/25.574/2.783 ms
5 packets transmitted, 5 received, 0% packet loss, time 4006ms
rtt min/avg/max/mdev = 13.600/16.195/20.912/2.708 ms
  endpoint: 10.252.156.2:51820
  latest handshake: 1 minute, 22 seconds ago
  transfer: 63.27 KiB received, 113.47 KiB sent
nuttx 237b6e538eb8e11a05a044105dc2507800802862
apps fc640afc4c9197972fca68495110429a6b8702a2
```
