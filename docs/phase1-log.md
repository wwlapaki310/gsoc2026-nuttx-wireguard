# Phase 1 開発ログ: ビルドシステム統合

## 目標

wireguard-lwip のソースを `apps/netutils/wireguard/` に配置し、`CONFIG_NET_WIREGUARD=y` でビルドが通る状態にする。

---

## 想定していたこと

- wireguard-lwip は `.c`/`.h` ソースのみを提供しており、NuttX のビルドシステム（`CMakeLists.txt`・`Make.defs`・`Kconfig`・`Makefile`）を自分で書けば統合できる
- NuttX は lwIP ベースのネットワークスタックを持つため、wireguard-lwip の `wireguardif.c` はほぼそのまま使える（[feasibility.md](feasibility.md) の想定）
- `sim:net` という設定名で NuttX シミュレータのネットワーク環境が使える

---

## 実際の手順とエラー対応

### 1. Docker ビルド環境の整備

#### エラー: `arm_netinitialize` undefined reference

```
arm-none-eabi-ld: undefined reference to `arm_netinitialize'
```

**原因:** `CONFIG_NET=y` を有効にすると `arm_initialize.c` が `arm_netinitialize()` を呼び出すが、virtio-net ドライバはこの関数を提供しない。

**対応:** `CONFIG_NETDEV_LATEINIT=y` を追加。ネットワーク初期化を遅延させることで呼び出しをスキップし、virtio-net がバスプローブ時に自己初期化するようにした。

---

#### エラー: `sim:net` が存在しない

```
Directory for sim:net does not exist.
```

**原因:** `sim:net` という設定名は NuttX 12.7.0 に存在しない。

**対応:** `sim:nsh` を使い、`kconfig-tweak` で `CONFIG_NET` 等を手動で有効化する方式に変更。

---

#### エラー: `genromfs: command not found`

**原因:** sim ターゲットは ROM ファイルシステムの生成に `genromfs` を使うが、Dockerfile に含まれていなかった。

**対応:** `apt-get install genromfs` を追加。

---

#### エラー: `xxd: command not found` / `zlib.h: No such file or directory`

**原因:** sim ターゲットのビルドに必要なツール・ライブラリが不足。

**対応:** `apt-get install xxd zlib1g-dev` を追加。

---

#### `ifconfig` で何も表示されない

**原因:** `CONFIG_NET=y` だけでは TUN/TAP 仮想 NIC が作られない。sim 用ネットワークドライバを有効化する設定が必要。

**対応:** `CONFIG_SIM_NETDEV=y` を追加。これにより `eth0 (10.0.0.2)` が表示されるようになった。

---

### 2. Dockerfile のマルチステージ化

sim と qemu の2ターゲットを1つの Dockerfile にまとめる構成（`--target sim` / `--target qemu`）に変更。wireguard-lwip のクローンと `apps/netutils/wireguard/` のセットアップは共通の `base` ステージで行う。

---

### 3. apps/netutils/wireguard/ のビルドシステム構築

#### エラー: `No rule to make target 'context'`

```
make[3]: *** No rule to make target 'context'.  Stop.
make[2]: *** [Makefile:53: /opt/apps/netutils/wireguard_context] Error 2
```

**原因:** NuttX の make ベースビルドシステムは各アプリに `Makefile`（`context`・`depend`・`clean` ターゲットを持つ）が必要だが、作成していなかった。

**対応:** `Makefile` を追加（`include $(APPDIR)/Make.defs` + `include $(APPDIR)/Application.mk`）。

---

### 4. 最大の発見: NuttX は lwIP を使っていない

#### エラー: `lwip/netif.h: No such file or directory`

```
./wireguard.h:41:10: fatal error: lwip/netif.h: No such file or directory
```

**原因の調査:**

```bash
find /opt/nuttx/include -name 'netif.h'   # → 何もヒットしない
find /opt/nuttx -name 'lwip' -type d      # → 何もヒットしない
```

**判明したこと:** NuttX 12.7.0 は upstream lwIP を使っていない。NuttX 独自のネットワークスタックを持っており、`lwip/netif.h`・`lwip/udp.h` などの lwIP ネイティブ API は公開ヘッダに存在しない。

これは当初の想定（「NuttX の LwIP も同じ API を使っているためほぼそのまま使える」）が誤りであったことを意味する。

**アーキテクチャへの影響:**

| wireguard-lwip | NuttX での対応 |
|----------------|---------------|
| `lwip/netif.h` → `netif_add()` | `net/netdev/netdev.h` → `netdev_register()` |
| `lwip/udp.h` → `udp_new/bind/recv` | BSD socket API (`sys/socket.h`) |
| `lwip/pbuf.h` → `pbuf_alloc/free` | `nuttx/net/iob.h` → `iob_alloc/free` |
| `lwip/timeouts.h` → `sys_timeout()` | `nuttx/wdog.h` → `wd_start()` |

**対応:**

1. `wireguardif.c`（upstream lwIP API に依存）はビルドから除外
2. `nuttx-wireguardif.c` を新規作成（Phase 2 で NuttX netdev API + BSD socket を使って実装）
3. `wireguard.c` 本体が参照する lwIP の型定義（`ip_addr_t`, `u16_t`, `struct netif`, `struct udp_pcb`）は最小限の互換ヘッダーで提供

---

#### エラー: `lwip/ip_addr.h: No such file or directory`（互換ヘッダー内のパス問題）

```
lwip/netif.h:11:10: fatal error: lwip/ip_addr.h: No such file or directory
```

**原因:** `lwip/netif.h` の中で `#include "lwip/ip_addr.h"` と書いていたが、`netif.h` 自体が `lwip/` ディレクトリ内にあるため、コンパイラは `lwip/lwip/ip_addr.h` を探してしまう（二重パス）。

**対応:** `lwip/netif.h` 内の include を `#include "ip_addr.h"` に修正（同ディレクトリ内の相対パス）。

---

## Phase 1 完了時の状態

- `CONFIG_NET_WIREGUARD=y` でビルド成功
- コンパイル対象: `wireguard.c`, `crypto/refc/*.c`, `nuttx-platform.c`（スタブ）, `nuttx-wireguardif.c`（スタブ）
- `sim` イメージ起動 → `eth0 (10.0.0.2)` 確認済み

```
nsh> ifconfig
eth0    Link encap:Ethernet HWaddr 42:67:c6:69:73:51 at RUNNING mtu 1500
        inet addr:10.0.0.2 DRaddr:10.0.0.1 Mask:255.255.255.0
```

---

## Phase 2 への引き継ぎ事項

- `wireguardif.c` は使わない。NuttX netdev API で `nuttx-wireguardif.c` を実装する
- `nuttx-platform.c` の4関数を本実装する（`clock_gettime`, `/dev/urandom`）
- `wireguard_device` 構造体の `struct netif *` と `struct udp_pcb *` は Phase 2 で NuttX 側の型に対応させる
