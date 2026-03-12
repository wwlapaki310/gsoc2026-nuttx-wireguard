# Feasibility Study: WireGuard Port to NuttX

> Status: Research phase (pre-GSoC)

---

## 1. WireGuard の動作原理

WireGuard は UDP 上で動作する L3 VPN プロトコル。  
Linux カーネルでは `wg0` という仮想 NIC として実装されている。

```
パケットの流れ（送信側）:

アプリケーション
    ↓ 通常の IP パケット
LwIP ルーティング
    ↓ 宛先が VPN 内 → wg0 netif へ
WireGuard 処理
    ↓ ChaCha20-Poly1305 で暗号化
    ↓ UDP パケットにカプセル化
物理 NIC (eth0/wlan0)
    ↓ インターネットへ送信
```

---

## 2. 暗号アルゴリズム

| アルゴリズム | 用途 | NuttX での対応状況 |
|-------------|------|-------------------|
| Curve25519 | 鍵交換（ECDH） | wireguard-lwip 内包実装を使用予定 |
| ChaCha20-Poly1305 | データの暗号化+認証 | mbedTLS で対応 (`CONFIG_MBEDTLS=y`) |
| BLAKE2s | ハッシュ | wireguard-lwip 内包実装を使用予定 |
| HKDF | 鍵導出 | mbedTLS で対応 |

NuttX には `CONFIG_MBEDTLS` があり、ChaCha20-Poly1305 と HKDF はカバー済み。  
Curve25519 と BLAKE2s は wireguard-lwip が内包する実装（OS 依存なし）をそのまま使える見込み。

---

## 3. wireguard-lwip のアーキテクチャ

参照実装 [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) の構造:

```
wireguard-lwip/
├── wireguard.c       # メイン処理（handshake, パケット暗号化/復号）
├── wireguard.h
├── wireguardif.c     # LwIP netif インターフェース層 ← NuttX と接続するポイント
├── wireguardif.h
├── crypto/
│   ├── chacha20poly1305.c
│   ├── curve25519.c   # OS 依存なし
│   └── blake2s.c      # OS 依存なし
└── ...
```

`wireguardif.c` が LwIP の `netif_add()` を呼び出して仮想 NIC を登録する。  
NuttX の LwIP も同じ API を使っているため、**ここはほぼそのまま使える見込み**。

---

## 4. NuttX との差異（ポーティングが必要な箇所）

### 4.1 タスク/スレッド

wireguard-lwip は FreeRTOS または bare-metal 想定で書かれた箇所がある。

```c
// FreeRTOS 版
xTaskCreate(wireguard_task, "wg", 4096, NULL, 5, NULL);

// NuttX 版に書き換える
pthread_t tid;
pthread_create(&tid, NULL, wireguard_task, NULL);
```

### 4.2 mutex

```c
// FreeRTOS
xSemaphoreCreateMutex();

// NuttX (POSIX)
pthread_mutex_t mutex;
pthread_mutex_init(&mutex, NULL);
```

### 4.3 時刻取得

WireGuard の handshake はリプレイ攻撃対策のためタイムスタンプを使う。

```c
// NuttX
struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);
```

### 4.4 乱数生成

```c
// NuttX — CONFIG_DEV_URANDOM=y が必要
int fd = open("/dev/urandom", O_RDONLY);
read(fd, buf, len);
close(fd);
```

---

## 5. メモリ見積もり

| コンポーネント | Flash (推定) | RAM (推定) |
|--------------|-------------|------------|
| wireguard-lwip | ~30 KB | ~8 KB |
| crypto (Curve25519, BLAKE2s) | ~20 KB | ~2 KB |
| WireGuard セッション状態 (1ピア) | — | ~512 B |
| **合計** | **~50 KB** | **~11 KB** |

ESP32-S3 (512 KB RAM, 8 MB Flash) には十分収まる。

---

## 6. 未解決の技術的疑問

- [ ] NuttX の LwIP バージョンは wireguard-lwip が期待するバージョンと一致するか？
- [ ] `CONFIG_NET_UDP` と `CONFIG_NET_IPv4` を有効化すれば UDP ソケットは動くか？
- [ ] ESP32-S3 の Wi-Fi ドライバと virtio-net を両方 netif として共存させられるか？
- [ ] WireGuard の handshake タイムアウト（180秒）は NuttX のタスクスケジューラと相性が良いか？
- [ ] `CONFIG_DEV_URANDOM` は QEMU arm シミュレータで使えるか？

---

## 7. 開発計画（175時間）

| フェーズ | 内容 | 時間 |
|---------|------|------|
| 1 | 暗号ライブラリ確認・ビルド | ~40h |
| 2 | wireguard-lwip の NuttX 移植（QEMU） | ~60h |
| 3 | NSH コマンド統合・Kconfig | ~35h |
| 4 | 実機テスト（ESP32-S3）・ドキュメント | ~40h |

---

## 8. 参照リンク

- [wireguard-lwip](https://github.com/smartalock/wireguard-lwip)
- [WireGuard-ESP32-Arduino](https://github.com/ciniml/WireGuard-ESP32-Arduino)
- [WireGuard Whitepaper](https://www.wireguard.com/papers/wireguard.pdf)
- [NuttX LwIP 設定](https://nuttx.apache.org/docs/latest/components/net/index.html)
