# Development Roadmap: WireGuard Port to NuttX

## 全体像

ゴールは「NuttX 上で `wg0` という仮想 NIC が動き、Linux ホストと WireGuard トンネルが張れる状態」。

```
最終的に動かしたいもの:

  NuttX (QEMU or ESP32-S3)
    wg0: 10.0.0.2/24  ←── WireGuard トンネル ──→  Linux PC
    eth0: 192.168.x.x                               wg0: 10.0.0.1/24

  nsh> ping 10.0.0.1   → 通る
```

---

## Phase 1: ビルド環境の確立（~週2）

**何をするか**: wireguard-lwip のソースが NuttX のビルドシステムで"コンパイルできる"状態にする。リンクや動作はまだ不要。

```
やること:
  1. wireguard-lwip の .c/.h を NuttX apps/ 以下に配置する
     → apps/netutils/wireguard/ に置くのが NuttX の作法

  2. CMakeLists.txt / Make.defs を書く
     → NuttX のビルドシステムは Kconfig + make/cmake のハイブリッド
     → 既存の apps/netutils/webserver/ 等を参考にする

  3. arm-none-eabi-gcc でコンパイルエラーを潰す
     → uint8_t/uint32_t の定義差異、__attribute__ の有無など軽微なものが多い

確認方法:
  $ make -j$(nproc)  # ビルドエラーが wireguard 関連で出ないこと
```

---

## Phase 2: OS 依存部分の NuttX 対応（~週4）

**何をするか**: wireguard-lwip が FreeRTOS や bare-metal を前提にしている部分を NuttX の API に書き換える。

```
書き換えが必要な箇所:

① スレッド生成
  // FreeRTOS
  xTaskCreate(wg_task, "wg", 4096, NULL, 5, NULL);
  // NuttX
  pthread_create(&tid, NULL, wg_task, NULL);

② mutex
  // FreeRTOS
  xSemaphoreCreateMutex();
  // NuttX
  pthread_mutex_t mtx;
  pthread_mutex_init(&mtx, NULL);

③ 時刻取得 (リプレイ攻撃対策のタイムスタンプ)
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

④ 乱数 (秘密鍵生成)
  int fd = open("/dev/urandom", O_RDONLY);
  read(fd, buf, len);
  close(fd);

確認方法:
  QEMU 上の nsh> で wireguard の初期化ログが出ること
  (クラッシュせず起動できれば Phase 2 クリア)
```

---

## Phase 3: LwIP netif への登録（~週6）

**何をするか**: WireGuard を LwIP の仮想 NIC (netif) として登録し、`ifconfig` で `wg0` が見えるようにする。

```
ポイント:
  wireguardif.c が提供する wireguardif_init() を呼ぶだけで
  LwIP に wg0 が登録される仕組みになっている。

  struct netif wg_netif;
  wireguardif_init(&wg_netif, &wg_config);
  netif_add(...);

  NuttX の LwIP も同じ API なので、ここは比較的ストレートに動くはず。

確認方法:
  nsh> ifconfig
  → wg0 が表示されること
  nsh> wg show   (Phase 4 で実装)
```

---

## Phase 4: ハンドシェイクとトンネル疎通（~週9）

**何をするか**: 実際に Linux ホストの WireGuard と鍵交換（ハンドシェイク）を行い、暗号化トンネルを通じて ping を通す。ここが Midterm の成果物。

```
流れ:
  1. NuttX 側と Linux 側で鍵ペアを生成
     Linux: wg genkey | tee privkey | wg pubkey > pubkey

  2. 互いの公開鍵と endpoint を設定
     NuttX 側は Kconfig または nsh コマンドで設定

  3. ハンドシェイク
     Initiator (NuttX) が Responder (Linux) に接続を開始
     → UDP 51820 を使って Noise プロトコルで鍵交換

  4. ping
     nsh> ping 10.0.0.1   # Linux の wg0 アドレス

確認方法:
  Linux 側: sudo wg show
  → NuttX との peer が "latest handshake" を持っていること
  NuttX 側: nsh> ping 10.0.0.1 → 応答があること
```

---

## Phase 5: NSH コマンドと Kconfig 統合（~週11）

**何をするか**: `wg` コマンドを NuttX Shell (nsh) に追加し、menuconfig から有効/無効を切り替えられるようにする。

```
目標:
  nsh> wg show
  interface: wg0
    public key: xxxx
    listening port: 51820

  peer: yyyy
    endpoint: 192.168.x.x:51820
    latest handshake: 5 seconds ago
    transfer: 1.23 KiB received, 0.45 KiB sent

Kconfig:
  config NET_WIREGUARD
    bool "WireGuard VPN support"
    depends on NET && NET_UDP && MBEDTLS
```

---

## Phase 6: 実機テストと upstream PR（~週12）

**何をするか**: ESP32-S3 実機で動作確認し、apache/nuttx-apps に PR を出す。

```
ESP32-S3 固有の考慮点:
  - Wi-Fi ドライバ経由での netif 登録
  - フラッシュ/RAM の実測値確認
  - LTE モジュール接続時の動作確認 (オプション)

upstream PR:
  - apps/netutils/wireguard/ にコードを置く
  - Apache の CLA (Contributor License Agreement) に署名
  - NuttX コーディング規約 (CODING_STYLE) への準拠
```

---

## 各 Phase の依存関係

```
Phase 1 (ビルド)
    ↓
Phase 2 (OS 依存部分) ← ここが一番手間がかかる
    ↓
Phase 3 (netif 登録) ← LwIP 互換性の確認
    ↓
Phase 4 (トンネル疎通) ← Midterm 成果物
    ↓
Phase 5 (NSH/Kconfig)
    ↓
Phase 6 (実機 + upstream)
```

Phase 2〜3 が技術的に最もリスクが高い（未知数が多い）。  
Phase 1 は純粋にビルドシステムの作業なので進捗が出しやすい。

---

## 参照実装の読み方

wireguard-lwip を読む順番:

1. `wireguardif.h` — 外部から呼ぶ API の一覧
2. `wireguardif.c` — LwIP netif として登録する部分 (Phase 3 の核心)
3. `wireguard.c` — ハンドシェイクとパケット処理 (Phase 4 の核心)
4. `crypto/` — 暗号プリミティブ (基本的に触らなくてよい)
