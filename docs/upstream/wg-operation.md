# Configuring and operating the in-kernel `wg`

Operational reference for the in-kernel version's `wg` command (`apps/system/wg`). The
command-level usage is also in the driver/command reference under `Documentation/` in the nuttx
fork; keep the two in sync. **IPv4 only.**

## Configuration keys

`wg setconf` / `saveconf` use the `wg(8)` INI layout (`[Interface]` / `[Peer]`). Recognized keys:

| Section | Supported (applied) | Ignored (silently, wg-quick's) | Rejected |
|---|---|---|---|
| `[Interface]` | `PrivateKey`, `ListenPort`, `Address` | `DNS`, `MTU`, `Table`, `PostUp`/`PostDown`, `SaveConfig`, … | a malformed value for a supported key |
| `[Peer]` | `PublicKey`, `PresharedKey`, `AllowedIPs`, `Endpoint`, `PersistentKeepalive` | any other key | a malformed value for a supported key |

So a desktop config is a close starting point, but wg-quick-only keys do **not** take effect
(they are neither honoured nor an error). A bad value for a recognized key aborts `setconf`.

## A minimal interoperating example

Topology: NuttX `wg0` = `10.10.0.2/24`, a Linux peer `10.10.0.1`. Substitute your own keys and
the reachable UDP endpoints.

**NuttX side** (NSH):

```
nsh> wg genkey                       # -> NUTTX_PRIV ; NUTTX_PUB = its pubkey
nsh> wg set private-key <NUTTX_PRIV>
nsh> wg set listen-port 51820
nsh> wg set address 10.10.0.2/24
nsh> wg set peer <LINUX_PUB> \
        endpoint <LINUX_IP>:51821 \
        allowed-ips 10.10.0.1/32 \
        persistent-keepalive 25
nsh> wg up
nsh> wg show                          # expect a recent handshake and rx/tx > 0
```

**Linux side** (root; the peer):

```
[Interface]
PrivateKey = <LINUX_PRIV>
ListenPort = 51821
Address    = 10.10.0.1/24

[Peer]
PublicKey  = <NUTTX_PUB>
AllowedIPs = 10.10.0.2/32
Endpoint   = <NUTTX_IP>:51820
PersistentKeepalive = 25
```

`wg-quick up ./that.conf` (or `ip link add … type wireguard` + `wg setconf`). Then
`ping 10.10.0.2` from Linux, or `ping 10.10.0.1` from NuttX, should traverse the tunnel.

## Routing

- `AllowedIPs` is **cryptokey routing**: it is both the set of destinations sent to that peer
  and the set of source addresses accepted from it. For a single point-to-point link use the
  peer's tunnel `/32`.
- `Address` on `[Interface]` sets `wg0`'s own tunnel address/netmask; it is not the same as
  `AllowedIPs`.
- For traffic to networks beyond the peer, add routes toward `wg0` with the usual NuttX tooling;
  the driver does not install routes itself.

## Keys and the config file

- The driver's **private key is write-only** — `wg show`/`SIOCGWGIF` never return it. The `wg`
  command therefore keeps the private key in the **config file** as the source of truth: `wg set
  private-key` writes it there as well as pushing it to the device, and `saveconf` takes the
  private key from the file and everything else from the device. `showconf` prints
  `PrivateKey = (hidden)`.
- Protect the config file like any private key: it contains `PrivateKey` in clear.

## First setup and surviving a reboot

- `CONFIG_SYSTEM_WG_CONFIG_PATH` is the default file for `saveconf`/`setconf` (point it at a
  writable, persistent filesystem, e.g. `/data/wg0.conf`).
- To restore after a reboot, run `wg setconf` (+ `wg up`) from the board's start-up script
  against that path — the file is interchangeable with a desktop WireGuard config within the
  supported subset above.
- Private key and listen port (and `setconf`) are accepted **only while `wg0` is down**.

## Verify and troubleshoot

| Symptom | Check |
|---|---|
| `wg up` returns "no private key" | `wg set private-key` (or `setconf`) first |
| No handshake | endpoint reachable? (plain UDP to `<peer>:port`); both peers list each other's **public** keys; clocks sane |
| Handshake but no data past the first packet | on older NuttX, the `crypto/chachapoly` nonce bug — this tree carries the fix (see [chachapoly-nonce-draft.md](chachapoly-nonce-draft.md)) |
| A `wg set peer …` line is truncated | raise `CONFIG_LINE_MAX` (and `CONFIG_NSH_LINELEN`) to ≥ 160 |
| `setconf` fails with "bad value" | a recognized key has a malformed value; fix that line |
| Config lost after reboot | `CONFIG_SYSTEM_WG_CONFIG_PATH` must be on a persistent, writable FS, and the start-up script must run `setconf`/`up` |
