# Phase 1 開発ログ: ビルドシステム統合

## 目標

wireguard-lwip のソースを `apps/netutils/wireguard/` に配置し、`CONFIG_NET_WIREGUARD=y` でビルドが通る状態にする。

---

## 想定していたこと

- wireguard-lwip は `.c`/`.h` ソースのみを提供しており、NuttX のビルドシステム（`CMakeLists.txt`・`Make.defs`・`Kconfig`・`Makefile`）を自分で書けば統合できる
- wireguard-lwip のコードは以下の3層に分かれており、移植コストはそれぞれ異なる

```
wireguard.c + crypto/      OS非依存のプロトコル実装・暗号実装 → 変更不要
wireguard-platform.h       OS固有の4関数（時刻・乱数・タイマー・負荷判定） → NuttX POSIX APIに置き換え
wireguardif.c              lwIPとの接続層 → lwIP APIをNuttX APIに書き換え
```

- `wireguardif.c` は lwIP の `netif_add()`・`udp_new()` などを呼んでいるが、NuttX のネットワーク構造（`netdev_register()`・BSD socket）と対応関係があるため、**ロジックを再利用しながら API 呼び出し部分だけ置き換えられる**はず

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

**対応:** `sim:nsh` を使い、`kconfig-tweak` で `CONFIG_NET`・`CONFIG_SIM_NETDEV` 等を手動で有効化する方式に変更。

---

#### エラー: `genromfs: command not found` / `xxd` / `zlib.h`

**対応:** `apt-get install genromfs xxd zlib1g-dev` を追加。

---

#### `ifconfig` で何も表示されない

**原因:** `CONFIG_NET=y` だけでは TUN/TAP 仮想 NIC が作られない。

**対応:** `CONFIG_SIM_NETDEV=y` を追加。これにより `eth0 (10.0.0.2)` が表示されるようになった。

---

### 2. Dockerfile のマルチステージ化

sim と qemu の2ターゲットを1つの Dockerfile にまとめる構成（`--target sim` / `--target qemu`）に変更。

---

### 3. apps/netutils/wireguard/ のビルドシステム構築

#### エラー: `No rule to make target 'context'`

```
make[3]: *** No rule to make target 'context'.  Stop.
```

**原因:** NuttX の make ベースビルドシステムは各アプリに `Makefile`（`context`・`depend`・`clean` ターゲットを持つ）が必要だが、作成していなかった。

**対応:** `Makefile` を追加（`include $(APPDIR)/Make.defs` + `include $(APPDIR)/Application.mk`）。

---

### 4. lwip/netif.h が存在しない問題

#### エラー: `lwip/netif.h: No such file or directory`

```
./wireguard.h:41:10: fatal error: lwip/netif.h: No such file or directory
```

**背景:**

NuttX のネットワークスタックは lwIP（lightweight IP）とは**別物**。lwIP は FreeRTOS + ESP-IDF などで広く使われる組み込み向け TCP/IP ライブラリだが、NuttX は uIP 由来の独自ネットワークスタックを持っており、`lwip/netif.h` のような lwIP のパブリックヘッダーはインクルードパスに存在しない。

```bash
$ find /opt/nuttx -name "netif.h"
（何も出てこない）
```

**ただし、上位の BSD socket API（`socket()`・`bind()` など）は NuttX でも同じ形で使える。**

**wireguard.c が `#include "lwip/netif.h"` を要求する理由:**

`wireguard.c` は `struct wireguard_device` の中に `struct netif *netif` というフィールドを持っている。実際にはポインタとして保持するだけで、中身の実装には触れない。そのため完全な lwIP の定義は不要で、前方宣言だけあればコンパイルできる。

**対応:** `lwip/` という名前のディレクトリに最小限の互換シムヘッダーを自作して配置した。

```
apps/netutils/wireguard/
└── lwip/
    ├── netif.h     ← struct netif の前方宣言のみ
    ├── ip_addr.h   ← ip_addr_t などの最小型定義
    ├── arch.h      ← u8_t, u16_t, u32_t などの型定義
    └── udp.h       ← struct udp_pcb の前方宣言のみ
```

これにより `wireguard.c` 本体を変更せずにコンパイルが通る。

---

### 5. wireguardif.c の扱い方針

`wireguardif.c` は lwIP の実装関数（`netif_add()`・`udp_new()`・`pbuf_alloc()`・`sys_timeout()` など）を直接呼んでいるため、そのままコンパイルすることはできない。

ただし、**プロトコルのロジック（パケットの暗号化・復号、ハンドシェイクの処理フロー）は全部そのまま再利用できる。** 置き換えが必要なのは以下の API 呼び出し部分のみ：

| wireguardif.c（lwIP） | NuttX 対応 |
|----------------------|------------|
| `struct netif` | `struct net_driver_s` |
| `netif->output = fn` | `dev->d_ifup = fn` 相当 |
| `ip_input(pbuf, netif)` | `devif_input(dev)` |
| `netif_set_link_up()` | `netdev_carrier_on()` |
| `udp_new()` / `udp_bind()` / `udp_recv()` | BSD `socket()` / `bind()` / `recvfrom()` |
| `pbuf_alloc()` / `pbuf_free()` | `iob_alloc()` / `iob_free()` |
| `sys_timeout()` | `wd_start()` |

Phase 1 では `nuttx-wireguardif.c` をスタブとして作成し、ビルドを通した。Phase 2 で `wireguardif.c` のロジックを参照しながら NuttX API を使って実装する。

---

## Phase 1 完了時の状態

- `CONFIG_NET_WIREGUARD=y` でビルド成功
- コンパイル対象: `wireguard.c`, `crypto/refc/*.c`, `nuttx-platform.c`（スタブ）, `nuttx-wireguardif.c`（スタブ）
- sim イメージ起動 → `eth0 (10.0.0.2)` 確認済み

```
nsh> ifconfig
eth0    Link encap:Ethernet HWaddr 42:67:c6:69:73:51 at RUNNING mtu 1500
        inet addr:10.0.0.2 DRaddr:10.0.0.1 Mask:255.255.255.0
```

---

## Phase 2 への引き継ぎ事項

- `wireguardif.c` のロジックを参照しながら `nuttx-wireguardif.c` を実装する
  - プロトコル処理のフローはそのまま再利用
  - lwIP API の呼び出し箇所だけ NuttX API に置き換える
- `nuttx-platform.c` の4関数を本実装する（`clock_gettime`, `/dev/urandom`）

→ 実施内容と詰まった点は [phase2-log.md](phase2-log.md) を参照。
