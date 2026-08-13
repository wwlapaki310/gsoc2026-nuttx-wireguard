# Phase 3 開発ログ: 実ハンドシェイクとトンネル疎通

## 目標

本物の WireGuard ピア(Linux カーネルの `wireguard` モジュール)との間でハンドシェイクを成立させ、トンネル越しの疎通を確認する。まず sim (`sim:nsh` + TUN/TAP) 上で protocol/crypto の end-to-end 動作を確認し、その後 QEMU (`qemu-armv7a` + TAP) 上でも NuttX 自身のスケジューラで同じ handshake/ping を確認した。

---

## 環境選択: まず sim、次に QEMU

作業開始時に QEMU 環境を検証したところ、`docker-entrypoint-qemu.sh` 自体が壊れていることが判明した(このプロジェクトで QEMU ターゲットが実際に起動確認されたことは一度もなかった可能性が高い)。

### 見つかった QEMU 起動の不具合

1. **`-bios none` がパースエラーになる**: `qemu-system-arm: Could not find ROM image 'none'` — このバージョンの QEMU (8.2.2) は `-bios none` を特別扱いせず、文字通り `none` という名前のファイルを探そうとする。`-bios` オプション自体を削除することで解決(`-kernel` による直接ブートでは不要)。
2. **デフォルト NIC が ROM を要求してエラーになる**: `-net nic,model=virtio -net user,...`(旧形式)を使うと `qemu-system-arm: failed to find romfile "efi-virtio.rom"` で失敗する。`virt` マシンタイプのデフォルト virtio-net-pci デバイスが UEFI 用の option ROM を要求するが、このイメージには同梱されていない。`-netdev user,id=n0,... -device virtio-net-device,netdev=n0`(MMIO 版、PCI ではない)に切り替えることで ROM 不要になり解決。

この2点を `docker/docker-entrypoint-qemu.sh` に反映し、QEMU は `nsh>` プロンプトまで正常に起動するようになった。

### QEMU ネットワークの追加修正

当初は起動しても `ifup eth0` が `Failed` になり、`ifconfig` に `eth0` が表示されなかった。原因は NuttX 12.7 の設定 symbol と QEMU virtio-mmio/FDT 周りの依存が不足していたこと。

対応として `Dockerfile` の QEMU stage に以下を追加した:

- `CONFIG_DRIVERS_VIRTIO=y`
- `CONFIG_DRIVERS_VIRTIO_MMIO=y`
- `CONFIG_DRIVERS_VIRTIO_NET=y`
- `CONFIG_DEVICE_TREE=y`
- `CONFIG_LIBC_FDT=y`
- `CONFIG_DEV_SIMPLE_ADDRENV=y`
- `CONFIG_NET_LL_GUARDSIZE=32`

これにより QEMU 上で `eth0` が登録され、`wg0` も起動できるようになった。

なお、`docker-entrypoint-qemu.sh` の `-netdev user,...,hostfwd=udp::51820-:51820` を使った検証では Linux peer から送信した WireGuard UDP が guest 側に届かず、`latest handshake: (never)` のままだった。end-to-end 検証では hostfwd を使わず、Docker コンテナ内に TAP デバイス(`tapqemu`)を作成し、QEMU guest と Linux peer を同じ L2 ネットワークに置く方式に切り替えた。

---

## sim 上での実ハンドシェイク検証

### 環境構成

sim コンテナ(`--cap-add=NET_ADMIN --device=/dev/net/tun`)の中で、NuttX の `./nuttx` プロセスと、**本物の Linux カーネル WireGuard**(`ip link add wg0 type wireguard` — Docker Desktop のバックエンド Linux カーネルに `wireguard` モジュールが実際にロード可能なことを確認した上で使用。`wireguard-go` のようなユーザースペース実装ではない)を同じコンテナ内で共存させた。

- NuttX の sim netdriver は TUN デバイス(`tap0`)を作成するが、ホスト側(コンテナの Linux 側)には IP アドレスが割り当てられていないため、`ip addr add 10.0.0.1/24 dev tap0` で明示的に割り当てる必要があった(NuttX 側は `10.0.0.2`、`DRaddr:10.0.0.1` を自動設定する)。
- Linux 側に `wg0`(`10.10.0.1/24`)を作成し、NuttX の公開鍵をピアとして登録、`allowed-ips 10.10.0.2/32`、endpoint は NuttX からの着信で自動学習させた。
- NuttX 側は Kconfig でテスト用秘密鍵・Linux 側公開鍵・エンドポイント(`10.0.0.1:51820`)を設定。

### 発見した2つの実バグ

`wg` コマンド実行後、`wg show` を何度実行しても `latest handshake: (never)` のまま変化せず、`tcpdump -i tap0` でも **送信パケットが1つも観測されない**ことを確認。ここから `nuttx-wireguardif.c` 側の実装にデバッグ出力を仕込み、原因を特定した。

#### バグ1: `SO_RCVTIMEO` が `recvfrom()` に効かない

`wg_rx_thread`(旧実装)は `setsockopt(SOL_SOCKET, SO_RCVTIMEO, ...)` でタイムアウトを設定した上で `recvfrom()` をブロッキング呼び出しし、タイムアウトのたびにハンドシェイク/keepalive のタイマー処理 (`wg_run_timers()`) を回す設計だった。

`setsockopt()` 自体は `ret=0`(成功)を返すが、実際には `recvfrom()` が **タイムアウトを一切無視して永久にブロックする**ことをデバッグ出力で確認した。ピアから何も送られてこない限り `recvfrom()` が戻らず、`wg_run_timers()` が一度も呼ばれない ⇒ 最初のハンドシェイク開始パケットすら送信されない、という鶏卵問題になっていた。

**対応:** `SO_RCVTIMEO` に依存するのをやめ、`poll(&pfd, 1, WG_TIMER_MSECS)` でタイムアウト付き待機に変更。`poll()` はより広く実装されている待機手段であり、こちらは正しくタイムアウトした。

#### バグ2: `wg` コマンドの終了とともに detached pthread が動かなくなる

バグ1を修正した後も、まだ送信が起きなかった。デバッグ出力を追加したところ、興味深い挙動が判明した:

- `pthread_create()` の直後に `usleep(300000)` を仕込むと、バックグラウンドスレッド内の `printf` が実際に出力される(スレッド自体は起動している)
- しかし `usleep` を入れずに `wg` コマンドがすぐ `main()` から返って NSH タスクが終了すると、バックグラウンドスレッドは **1行も出力しないまま消える**(`ps` にも一切現れない)

つまり `wg` コマンド(NSH ビルトインタスク)が `pthread_create()` + `pthread_detach()` したスレッドは、**detach していても `wg` タスク自身の終了とともに実行されなくなる**ことが実測で確認された。これは strict POSIX の期待(detach されたスレッドは生成元スレッドの終了と無関係に生存する)とは異なる、この NuttX 構成 (sim) 特有の挙動と考えられる。原因の完全な特定はできていない(NuttX のタスクグループ管理の詳細に依存する可能性がある)。

**対応:** バックグラウンド処理を pthread ではなく `task_create()` による**独立したタスク**として起動するよう変更。`wg` コマンドのタスクとはライフタイムが切り離されるため、`wg` コマンドが終了した後もバックグラウンドタスク(`wg_rx`)が動き続けるようになった。`ps` にも独立したタスクとして表示されることを確認済み。

```c
ret = task_create("wg_rx", CONFIG_NET_WIREGUARD_PRIORITY,
                   CONFIG_NET_WIREGUARD_RX_STACKSIZE, wg_rx_task, NULL);
```

### 検証結果

両方の修正後、実際に本物の WireGuard ハンドシェイクが成立した:

**NuttX 側 (`wg show`):**
```
interface: wg0
  public key: KUGuHwdGvQfrb85m5+TaDLEOVRCvI3Ob9iUKsQdY+nA=
  listening port: 51820
peer: DyNgbWsgZ8BEBSsbccbpFw14Ly1ux39QSn4123wXI2I=
  endpoint: 10.0.0.1:51820
  latest handshake: 5 seconds ago
  transfer: 336 B received, 252 B sent
```

**Linux 側 (`wg show wg0`):**
```
peer: KUGuHwdGvQfrb85m5+TaDLEOVRCvI3Ob9iUKsQdY+nA=
  endpoint: 10.0.0.2:51820
  allowed ips: 10.10.0.2/32
  latest handshake: 4 seconds ago
  transfer: 180 B received, 92 B sent
```

**トンネル越しの疎通確認(Linux 側から `ping 10.10.0.2`):**
```
PING 10.10.0.2 (10.10.0.2) 56(84) bytes of data.
64 bytes from 10.10.0.2: icmp_seq=1 ttl=64 time=21.1 ms
64 bytes from 10.10.0.2: icmp_seq=2 ttl=64 time=12.1 ms
64 bytes from 10.10.0.2: icmp_seq=3 ttl=64 time=3.75 ms

--- 10.10.0.2 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss
```

暗号化・鍵交換・トンネル内 IP パケットの往復まで含め、実際の WireGuard プロトコルとして正しく相互運用できることを確認した。

---

## QEMU 上での実ハンドシェイク検証

### 環境構成

QEMU コンテナ(`nuttx-wireguard:qemu`)の中で TAP デバイスを作成し、QEMU guest の NuttX と Linux カーネル WireGuard peer を同じコンテナ内で接続した。

- Linux 側 TAP: `tapqemu` (`10.0.0.1/24`)
- QEMU NuttX 側 `eth0`: `10.0.0.2/24`
- Linux WireGuard peer: `wgtest0` (`10.10.0.1/24`)
- QEMU NuttX 側 WireGuard: `wg0` (`10.10.0.2/24`)
- Linux peer endpoint: `10.0.0.2:51820`
- NuttX peer endpoint: `10.0.0.1:51821`

検証スクリプト:

```bash
docker run --rm --cap-add=NET_ADMIN --device=/dev/net/tun \
  -v ${PWD}/scripts:/workspace/scripts:ro \
  --entrypoint bash nuttx-wireguard:qemu \
  /workspace/scripts/verify-qemu-wireguard.sh
```

### 検証結果

QEMU 上でも `eth0` と `wg0` が起動し、本物の Linux WireGuard peer と handshake した。

**QEMU NuttX 側 (`wg show`):**
```
interface: wg0
  public key: 52oFKVL3y5jNNmZ3cb5A9miMTx9JbPmQx6uV+ModzHU=
  listening port: 51820
peer: LO9bnmXH0WXGm8CXFj41rV8+vdCYlrt1ckll6TS4bUQ=
  endpoint: 10.0.0.1:51821
  latest handshake: 9 seconds ago
  transfer: 352 B received, 252 B sent
```

**Linux 側 (`wg show wgtest0`):**
```
peer: 52oFKVL3y5jNNmZ3cb5A9miMTx9JbPmQx6uV+ModzHU=
  endpoint: 10.0.0.2:51820
  allowed ips: 10.10.0.2/32
  latest handshake: 11 seconds ago
  transfer: 476 B received, 564 B sent
```

**トンネル越しの疎通確認(Linux 側から `ping 10.10.0.2`):**
```
PING 10.10.0.2 (10.10.0.2) 56(84) bytes of data.
64 bytes from 10.10.0.2: icmp_seq=1 ttl=64 time=34.8 ms
64 bytes from 10.10.0.2: icmp_seq=2 ttl=64 time=0.914 ms
64 bytes from 10.10.0.2: icmp_seq=3 ttl=64 time=0.960 ms

--- 10.10.0.2 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss
```

この結果により、Linux プロセスとして動く sim だけでなく、QEMU 上の ARM Cortex-A7/NuttX スケジューラ環境でも WireGuard の handshake、タイマー処理、UDP I/O、TUN netdev 注入が成立することを確認した。

---

## Phase 3 完了時の状態

- ✅ 本物の(wireguard-go ではない、Linux カーネルネイティブの)WireGuard ピアとの実ハンドシェイクを sim 上で確認
- ✅ トンネル越しの ICMP 疎通を双方向で確認(0% packet loss)
- ✅ `docker-entrypoint-qemu.sh` の起動不具合(`-bios none` / デフォルト NIC の ROM 要求)を修正し、QEMU が `nsh>` まで起動するようになった
- ✅ QEMU 上で `eth0` / `wg0` が起動し、Linux WireGuard peer との実ハンドシェイクと tunnel ping を確認
- ⚠️ QEMU の `hostfwd` 方式では WireGuard UDP が guest に届かなかったため、end-to-end 検証は TAP 方式で実施

## Phase 4 / 今後への引き継ぎ事項

- ESP32-S3 実機での Wi-Fi 経由 handshake / tunnel ping 検証
- 長時間 keepalive、再接続、MTU 境界、複数 peer の追加検証
- 今回 sim で見つかった2つのバグ(`SO_RCVTIMEO` 未実装、detached pthread の生存期間)は NuttX 自体の制限/挙動である可能性があるため、upstream(NuttX 本体)側で既知の issue か確認する価値がある
- `wg_rx` タスクの優先度は現在 `CONFIG_NET_WIREGUARD_PRIORITY`(wg コマンド自身の優先度設定)を流用している。専用の Kconfig 項目に分離するかは要検討
