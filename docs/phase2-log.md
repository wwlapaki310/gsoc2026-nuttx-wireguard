# Phase 2 開発ログ: NuttX 統合レイヤー実装

## 目標

Phase 1 で作成したスタブを実際の NuttX API で置き換え、WireGuard のハンドシェイクと
暗号/復号パスが SIM ターゲット上で動作する状態にする。

---

## 実装した内容

### 1. `nuttx-platform.c` — 4 つのプラットフォーム関数の本実装

| 関数 | 実装 |
|------|------|
| `wireguard_sys_now()` | `clock_gettime(CLOCK_MONOTONIC)` でミリ秒を返す |
| `wireguard_random_bytes()` | `/dev/urandom` を `read()` (CONFIG_DEV_RANDOM=y 必須) |
| `wireguard_tai64n_now()` | `clock_gettime(CLOCK_REALTIME)` + TAI64N フォーマット変換 |
| `wireguard_is_under_load()` | 組み込みターゲットでは常に `false` |

TAI64N は 8 バイトの秒 (2^62+10 オフセット付き) + 4 バイトのナノ秒でビッグエンディアン。
WireGuard の handshake リプレイ防止に使用。

---

### 2. `nuttx-wireguardif.h` — NuttX 向け公開 API

lwIP の `wireguardif.h` を置き換える NuttX ネイティブな API を定義。

```c
int  wg_netdev_init(const struct wg_device_config *cfg);
int  wg_netdev_add_peer(const struct wg_peer_config *cfg, uint8_t *idx);
int  wg_netdev_connect(uint8_t peer_idx);
void wg_netdev_shutdown(void);
int  wg_key_from_hex(uint8_t key[32], const char *hex);
int  wg_key_from_base64(uint8_t key[32], const char *b64);
```

---

### 3. `nuttx-wireguardif.c` — WireGuard トンネル実装

wireguard-lwip の `wireguardif.c` を NuttX API で書き直したもの。

**受信フロー:**

```
recvfrom(udp_fd)
  → wireguard_get_message_type()
  → switch:
      HANDSHAKE_INIT  → wireguard_process_initiation_message()
                        wireguard_create_handshake_response()
                        sendto(peer_ep)
      HANDSHAKE_RESP  → peer_lookup_by_handshake()
                        wireguard_process_handshake_response()
      COOKIE_REPLY    → wireguard_process_cookie_message()
      TRANSPORT_DATA  → peer_lookup_by_receiver()
                        get_peer_keypair_for_idx()
                        wireguard_check_replay()
                        wireguard_decrypt_packet()
                        [Phase 3: devif_input()]
```

**送信フロー (Phase 3 で完成):**

```
devif_poll() → wg_tx()
  → 送信先 peer を allowed_ip で検索
  → wireguard_encrypt_packet()
  → sendto(peer_ep)
```

**重要な変換点:**

| wireguard-lwip (lwIP) | 本実装 (NuttX) |
|-----------------------|---------------|
| `struct netif` | `struct net_driver_s` (Phase 3) |
| `struct udp_pcb` | `int udp_fd` (BSD socket) |
| `pbuf_alloc/free` | `uint8_t rx_buf[]` (静的バッファ) |
| `sys_timeout()` | keepalive スレッド (Phase 3) |
| `udp_recv()` コールバック | `pthread` + `recvfrom()` ループ |

**counter フィールドの扱い:**

`message_transport_data.counter` は `uint8_t[8]` (LE)。
`wireguard_decrypt_packet()` が受け取る `uint64_t` へ手動変換:

```c
for (i = 0; i < 8; i++) {
    counter |= (uint64_t)msg->counter[i] << (i * 8);
}
```

---

### 4. `wg_app.c` — NSH `wg` コマンド

```
nsh> wg init <privkey_hex> 10.0.1.1/24
nsh> wg addpeer <pubkey_hex> endpoint 192.168.1.100 51820 allowedips 10.0.1.2/32
nsh> wg connect 0
nsh> wg status
nsh> wg down
```

鍵は 64 文字 hex または 44 文字 base64 どちらでも受け付ける。

---

### 5. Dockerfile — qemu ステージに WireGuard 追加

```
kconfig-tweak --enable CONFIG_MBEDTLS
kconfig-tweak --enable CONFIG_DEV_RANDOM
kconfig-tweak --enable CONFIG_NET_WIREGUARD
kconfig-tweak --enable CONFIG_NET_WIREGUARD_APP
```

---

## wireguard-lwip API 調査メモ

Phase 2 実装時に実際の API シグネチャを確認した結果:

```c
// wireguard.h から (GitHub: smartalock/wireguard-lwip)
void wireguard_init(void);           // ← 最初に呼ぶ必要あり (Phase 1 スタブでは漏れていた)
struct wireguard_peer *wireguard_process_initiation_message(device, msg);  // bool ではなく peer* を返す
bool wireguard_process_handshake_response(device, peer, msg);  // peer 引数あり
bool wireguard_create_handshake_response(device, peer, dst);   // sender_index 引数なし
void wireguard_encrypt_packet(dst, src, src_len, keypair);
bool wireguard_decrypt_packet(dst, src, src_len, counter, keypair);
bool wireguard_base64_decode(str, out, outlen);  // ← 組み込み済み、自前不要
```

---

## Phase 2 完了時の状態

- `CONFIG_NET_WIREGUARD=y` + `CONFIG_NET_WIREGUARD_APP=y` でビルド可能
- sim/qemu 両ターゲット対応 Dockerfile
- WireGuard プロトコルの全メッセージタイプ (1〜4) を処理
- 復号したトランスポートデータを syslog に記録
- IP レイヤーへの注入は Phase 3 (`devif_input` 統合) で実施

---

## Phase 3 への引き継ぎ

- `devif_input()` でデクリプト済みパケットを NuttX IP スタックへ注入
- `net_driver_s` 登録 (`netdev_register(NET_LL_TUN)`)
- 送信パス: `d_txavail` コールバック → encrypt → `sendto()`
- keepalive タイマー (`wd_start()` または追加スレッド)
- QEMU (virtio-net 経由) での実機動作確認
