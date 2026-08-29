# コードレビューと今後の計画 (2026-08)

ESP32-S3 実機検証・TCP バグ修正が一段落した時点で、`nuttx_port/apps/netutils/wireguard/` の実装全体を読み直し、upstream 提出・発表・実用性の3つの観点から棚卸ししたもの。

---

## 1. 現状の評価

### 良い状態にあるもの

- **プラットフォーム抽象化 (`nuttx-platform.c`)**: `wireguard-platform.h` の4関数のみ。sim / QEMU / Xtensa (ESP32・ESP32-S3) / ARM Cortex-M4F (Spresense) すべてで無変更で動作しており、移植性の設計は正しかった
- **vendored コードの扱い**: `wireguard.c` / `crypto/` は upstream wireguard-lwip の BSD-3-Clause ヘッダを保持したまま未改変。`ALLOW_BSD_COMPONENTS` ゲートも含め、ライセンス面の設計判断は upstream の要求と合致している
- **netdev 統合 (`nuttx-wireguardif.c`)**: `NET_LL_TUN` netdev として `wg0` を登録し、UDP ソケットを「配線」として使う設計。実機で ICMP・TCP(telnet・HTTP)すべての疎通を確認済み
- **実機検証**: upstream の必須要件である「実機でのビルド・実行ログ」を ESP32-S3 で満たしている

### 発見した課題

以下は今回のレビューで新たに洗い出したもの。深刻度順。

#### (A) RX タスクのスタックサイズが実測に基づいていない

`CONFIG_NET_WIREGUARD_RX_STACKSIZE` のデフォルトは 3072。しかし `wg_rx_task` は `wg_inject_plaintext()` → `ipv4_input()` を自分のコンテキストで呼ぶため、**TCP パケットを処理する際に NuttX の TCP スタック全体の呼び出し深度がこのタスクのスタックに乗る**。

ESP32-S3 での TCP バグ調査中、3072 のままクラッシュ(`EXCCAUSE=0x1c`)が発生したため 8192 に引き上げて回避したが、その後 EBADF が真因と判明したため、**3072 が本当に不足していたのかは未検証のまま**。さらに 8192 という値も Dockerfile に永続化されておらず、コンテナ破棄とともに失われた。

→ `CONFIG_STACK_COLORATION` を有効にして実機で実測し、根拠のあるデフォルト値を Kconfig に設定する。

**【実測結果 2026-08-22】** ESP32-S3 実機で計測した:

| 状態 | `wg_rx` スタック消費 |
|---|---|
| `wg0` 起動直後(トラフィックなし) | 2,864 B |
| TCP 約 1.2 MB + 大サイズ ping フラッド後 | **2,896 B** |
| 上記を別ビルド(スタック 4096)で再現 | **2,896 B**(71.4%) |
| (C) 修正後・TCP 約 7 MB + ping フラッド | **2,960 B**(72.9%) |

- 2回の独立した計測で **ピークが 2,896 B ちょうどで一致**しており、再現性のある値
- **旧デフォルト 3072 では余裕がわずか 176 バイト (94.3% 使用)** — 実質的に不足していた。クラッシュの直接原因は EBADF だったが、スタックも同時に危険域にあった
- 新デフォルトを **4096** に変更(実測比 約 40% の余裕)。8192 は過剰

計測手順・根拠は `Kconfig` の `NET_WIREGUARD_RX_STACKSIZE` の help に記載し、ユーザーが自環境で再測定できるようにした。

#### (B) 転送バイト数カウンタの非対称性

```c
/* RX: 暗号文長 (認証タグ込み) を加算 */
priv->peer_rx_bytes[...] += data_len;

/* TX: 平文長を加算 */
priv->peer_tx_bytes[...] += plaintext_len;
```

送受信で数えている対象が違うため、`wg show` の `transfer:` 表示が本家 `wg(8)` と比較できない。本家はワイヤ上のバイト数(暗号文)を数えるので、TX 側を暗号文長に揃えるのが正しい。

#### (C) RX タスクが 50ms 間隔でポーリングし続ける → ✅ 修正済み

EBADF 修正の際、fd ベースの `poll()` が使えなくなったため `psock_recvfrom(MSG_DONTWAIT)` + `usleep(50ms)` のポーリングループにした。動作は正しいが、**アイドル時も毎秒 20 回 CPU を起こす**。バッテリー駆動の IoT デバイスでは無視できない。

**【修正 2026-08-22】** `psock_poll()` にコールバックを仕込み、セマフォでブロックする本来の待機に戻した。`struct pollfd` はこの NuttX ではセマフォではなくコールバック(`cb`/`arg`)方式なので、`wg_poll_cb()` が `nxsem_post()` するだけの薄いコールバックを渡し、`nxsem_tickwait()` でタイムアウト付きに待つ。teardown と次の setup の間で取りこぼす心配はない — `udp_pollsetup()` は read-ahead キューが空でなければ即座に通知する。

**効果は CPU 負荷の削減にとどまらなかった。** 50ms ポーリングは、届いたパケットが最大 50ms 待たされることを意味しており、これが全体のスループットを律速していた:

| | 50ms ポーリング | ブロッキング待機 |
|---|---|---|
| TCP スループット | 約 24 KiB/s | **約 260 KiB/s**(約 10 倍) |
| 負荷時 ping RTT | 370〜1146 ms | **33〜68 ms** |
| `wg_rx` タスク状態 | `Waiting Signal`(usleep) | `Waiting Semaphore` |

副産物として、7 MB の連続転送中に **rekey(再ハンドシェイク)が発生して成功**することも確認できた。これまで未検証だった経路。

#### (D) 実行時設定ができない (UX 上の最大の弱点) → ✅ 実装済み

秘密鍵・ピア公開鍵・エンドポイントがすべて Kconfig の固定値で、**変更するたびにリビルド + 書き込みが必要**だった。実際、実機デモの準備でこれが最も時間を食った。加えて:

- 秘密鍵が `.config` とビルド成果物に焼き込まれる(`defconfig` をコミットすると鍵が漏れる)
- `wg0` の起動が手動 (`wg` コマンド) で、再起動のたびに打ち直しが必要 → 起動時自動化は rcS で解決済み([hardware-verification.md](hardware-verification.md) のヘッドレス運用節)

**【実装 2026-08-29】** 本家 `wg(8)` に寄せたサブコマンドを追加した:

```
wg genkey                    # デバイス上で秘密鍵を生成
wg pubkey <private-key>      # 対応する公開鍵を導出
wg set private-key <key>
wg set peer <public-key> [endpoint <addr:port>]
                            [allowed-ips <addr>/<prefix>]
                            [persistent-keepalive <sec>]
wg up / wg down / wg show
```

設計上のポイント:

- **Kconfig は既定値として残る。** `wg set` で明示的に指定した項目だけが上書きされ、未指定の項目は `CONFIG_NET_WIREGUARD_*` を使う。既存の Kconfig 完結型ビルド(rcS 自動起動を含む)は挙動が変わらない
- **ステージング領域は `struct wg_netdev_s` の外に置いた。** `wg_initialize()` がこの構造体を丸ごと `memset` するのと、`wg down` → `wg up` で設定が Kconfig に巻き戻ってはいけないため
- **`wg set` は down 中のみ許可** (`-EBUSY`)。稼働中のピア差し替えは RX タスクとの競合を招くため、`down` → `set` → `up` の流れに限定した
- **vendored コードは無改変を維持。** `wireguard_clamp_private_key()` / `wireguard_generate_public_key()` は `wireguard.c` の `static` 関数で外から呼べないため、clamp(RFC 7748 の仕様定数2行)と基点スカラー倍を `crypto.h` が公開する `wireguard_x25519()` を使って自前実装した

実機検証(ESP32-S3):

- `wg genkey` は毎回異なる鍵を返す(エントロピー源が機能している)
- `wg pubkey <Kconfig の秘密鍵>` の出力が `wg show` の interface public key と**完全一致** — vendored 実装と独立に導出した結果が一致したので、導出が正しいことの裏付けになる
- `wg down` で `wg0` が `ifconfig` から消え、再度 `wg down` は `-ENODEV`
- `wg down` → `wg set peer ...` → `wg up` で**リビルドなしにトンネルを張り直し**、ハンドシェイク成立・ping 0% packet loss を確認

**副産物として `CONFIG_NSH_LINELEN` の問題を発見した。** 既定値 64 に対し
`wg set peer <44 文字の base64 鍵> endpoint ... allowed-ips ... persistent-keepalive ...`
は約 134 文字あり、**行が途中で切られて残りが別コマンドとして解釈されていた**。
160 に引き上げて解決(`Dockerfile` に永続化)。実行時設定を使うなら必須の設定。

**【追加実装 2026-08-29】設定の永続化。** 上記の実行時設定は RAM 上のみで、電源を切ると
Kconfig の値に戻っていた。ヘッドレス運用では「停電のたびに再設定」を意味するため、機能として未完だった。

独自形式を作らず、**本家 `wg(8)` と同じ INI 形式**を採用した:

```
wg saveconf                      # 保存 (既定 /data/wg0.conf)
wg setconf /data/wg0.conf        # 復元
```

これにより:

- **デスクトップの WireGuard クライアントと設定ファイルが相互に読める** — 独自形式なら得られない利点
- 新しいストレージ API を作る必要が無い。既存のファイル I/O だけで完結する
- rcS が `wg up` の前にこのファイルを読む(`-f` で存在確認するので、未設定のボードは Kconfig 値にフォールバックする)

保存先は `CONFIG_NET_WIREGUARD_CONFIG_PATH`(既定 `/data/wg0.conf`)。**esp32s3-devkit は既に SPIFFS を `/data` にマウントしている**ため、ボード側・パーティション側の変更は一切不要だった。

パーサは知らないキーを無視する設計にした。`Address` / `DNS` / `MTU` といった wg-quick 専用のディレクティブが入っていても、デスクトップの設定ファイルをそのまま読み込める。セクション見出しも読み飛ばす — 単一ピアでは `[Interface]` と `[Peer]` を区別する意味が無いため。

sim で round-trip を検証済み: `/24` プレフィックスと非デフォルトの keepalive を設定 → `showconf` → ファイル → `setconf` → `showconf` が**完全に同一の出力**を返すことを確認(ネットマスク↔プレフィックス変換とパーサが整合している)。

**【実機検証 2026-08-30】** ESP32-S3 実機で**電源断をまたぐ復元を確認**した。
Kconfig では表現できない設定(keepalive 33 / 44 の2ピア — Kconfig は単一ピアで既定 25)を
保存 → ハードリセット → 起動後に `wg showconf` が同じ内容を返し、実ピアとのハンドシェイクも
21秒後に成立、トンネル越し ping 0% packet loss。

**この過程で、当初の保存方法が実機で使えないことが判明した。**
`wg showconf > /data/wg0.conf` は **ESP32-S3 をハングさせる**(シリアルもネットワークも
無応答になり、リセット以外に復帰手段がない。ファイルも書かれない)。切り分けると:

| 試したこと | 結果 |
|---|---|
| `echo foo > /data/x.txt`(NSH 内部コマンド) | ✅ 正常 — SPIFFS 書き込み自体は問題ない |
| 存在しないパスへリダイレクト | ✅ エラーを返す — リダイレクト自体も問題ない |
| `wg show > /data/y.txt`(builtin アプリ) | ❌ ハング。`wg0` の up/down に関係なく再現 |
| `CONFIG_ESP32S3_SPIFLASH_OP_TASK_STACKSIZE` を 768→3072 | ❌ 改善せず |

NSH は builtin アプリを**別タスクとして spawn** し、その stdout をリダイレクトする。
この組み合わせが SPIFFS 相手だと固まる。コンポーネント側で制御できる範囲の外(プラットフォーム
レベルの相互作用)と判断した。

→ **シェルのリダイレクトに依存しない `wg saveconf` を追加**し、アプリ自身が `fopen`/`fprintf`
する方式にした。これは動作する。`wg setconf` が元々自前で `fopen` していたのと対称でもある。
出力処理は `wg_writeconf(FILE *)` に一本化し、**表示内容と保存内容が食い違わない**ようにした。

なお `spiflash_op` のスタックは 3072 に上げたまま残している(実機で 79.5% / 80.4% と
NuttX の `!` 警告が出ていたため。3072 で 18.6% になる)。ハングの原因ではなかったが、
保存がこの経路を通る以上、余裕を持たせておく価値はある。

#### (E) upstream スタイル準拠

ファイルヘッダに ASF ライセンスブロックがなく、関数コメントも `Input Parameters:` / `Returned Value:` が揃っていない箇所がある。`tools/checkpatch.sh` / `nxstyle` を通していない。upstream 提出の機械的な前提条件。

#### (F) 停止・再設定手段がない → ✅ 実装済み

`priv->running` を false にする経路がなく、`wg` は一度上げたら落とせなかった(`wg_ifdown` は netdev を down にするだけで RX タスクは回り続ける)。

**【実装 2026-08-29】** `wg down` を追加。順序が重要で:

1. `priv->running = false` で RX タスクにループ脱出を要求
2. **タスクが `rxtask_done` を立てるのを待つ** — ソケットは稼働中の RX タスクが所有しており、待たずに `psock_close()` すると実行中の `psock_recvfrom()` の足元を崩す
3. `work_cancel()` で積み残しの TX ワークを取り消し
4. `netdev_unregister()` → `psock_close()`

タイムアウト時は `-ETIMEDOUT` を返して `running` を戻し、**中途半端に壊れた状態にしない**。

---

## 2. 今後の計画

優先度は「upstream 提出への距離」×「実装コスト」で判断した。

### 第1段階: 実機で測れることを測る (今回着手)

| 項目 | 内容 |
|---|---|
| (A) スタック実測 | `CONFIG_STACK_COLORATION` を有効化し、TCP トラフィックを流した状態で `wg_rx` の消費量を実測。根拠のある Kconfig デフォルトを設定 |
| (B) カウンタ修正 | TX 側を暗号文長に統一 |
| Dockerfile 永続化 | 実機検証で必要だった Kconfig(スタックサイズ・MTU)を `esp32s3` ステージに反映し、コンテナ破棄で失われないようにする |

実機が手元にある今しかできない作業を優先する。

### 第2段階: upstream 提出の前提を埋める

| 項目 | 内容 |
|---|---|
| (E) スタイル準拠 | `checkpatch.sh` / `nxstyle` を通し、ASF テンプレートに合わせて4ファイルを整形 |
| `LICENSE` 追記案 | wireguard-lwip の著作権表示を `apache/nuttx-apps/LICENSE` の Appendix パターンで追記する差分を用意 |
| `Assisted-by:` 運用 | 以後のコミットに `Assisted-by: Claude:claude-sonnet-5` を付与(ASF の生成 AI ポリシー準拠) |

詳細は [upstream-strategy.md](upstream-strategy.md) の §1・§4 を参照。

### 第3段階: 実用性・堅牢性 (GSoC 本期間の中心)

| 項目 | 状態 |
|---|---|
| (D) ランタイム設定 | ✅ `wg genkey` / `pubkey` / `set` を実装 |
| (F) `wg down` | ✅ 実装 |
| netinit 連携 | ✅ rcS による起動時自動立ち上げ |
| (C) ポーリング解消 | ✅ `psock_poll()` ベースのブロッキング待機 |
| 設定の永続化 | ✅ `wg showconf` / `wg setconf`(wg(8) 互換 INI 形式)+ rcS で復元 |
| 複数ピア | ❌ 現状 vendored ヘッダの `WIREGUARD_MAX_PEERS`(=1)に依存 |
| 異常系検証 | ❌ 長時間 keepalive・再接続・MTU 境界 |
| ビルドモード | ❌ `psock_*()` 依存で FLAT ビルド前提。PROTECTED/KERNEL は別設計 |

### 第4段階: upstream PR

[upstream-strategy.md](upstream-strategy.md) §3 の段階的 PR 戦略に従う。その前に dev@nuttx.apache.org でアーキテクチャの合意を取る。

---

## 3. 発表資料

[presentation-script.md](presentation-script.md) に発表原稿をまとめた。技術的な山場は「ping は通るのに TCP だけ無言で死ぬ」バグの調査過程(`EBADF` / タスクグループとファイルディスクリプタのスコープ)。
