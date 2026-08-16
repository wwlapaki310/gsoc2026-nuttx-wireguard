# Phase 4 まとめ: 実機 WireGuard 実証とデモ動画

## デモ動画

[https://youtu.be/1kyX2av5WG4](https://youtu.be/1kyX2av5WG4)

ESP32-S3 実機 → 実 Wi-Fi → WireGuard トンネル → telnet ログイン → コマンド実行 → Web サーバー起動 → ブラウザアクセス、という一連の流れを収録したもの。

## 達成したこと

- **実機・実 Wi-Fi・実ピアでの WireGuard ハンドシェイク**: ESP32-S3 を実際の家庭用アクセスポイントに接続し、Windows 公式 WireGuard クライアント(Linux カーネル実装ではない、異実装ピア)との間でハンドシェイクを成立させた
- **トンネル越し ping**: 0% packet loss
- **TCP 特有バグの発見・修正**: ping(ICMP)は通るのに telnet(TCP)のデータだけがトンネルを通らないバグを発見。原因は NuttX のファイルディスクリプタがタスクグループごとにスコープされる制約で、LPWORK ワーカースレッドから `sendto()` すると `EBADF` になっていたこと。`psock_*()` 内部 API(ファイルディスクリプタを経由しない `struct socket` ベースの API)への切り替えで解決した。詳細は [docs/phase4-log.md](phase4-log.md) の「トンネル越し telnet で見つかった TCP 特有バグの調査・修正」を参照
- **トンネル越し telnet でのコマンド実行**: 修正後、telnet セッションで `uname -a` / `ifconfig` / `wg show` / `free` などを実行し、実機からの応答を確認
- **トンネル越し Web サーバーアクセス**: NuttX 標準の uIP webserver(`apps/examples/webserver`)を起動し、ブラウザから `http://10.10.0.2/` でアクセスできることを確認。ICMP・TCP(対話コマンド)・TCP(HTTP)の3種類の通信すべてがトンネル越しに動作することを実証した

## 技術的なポイント(短く)

`nuttx-wireguardif.c` の UDP ソケットは、当初 `socket()`/`recvfrom()`/`sendto()`/`poll()` という通常の fd ベース API で実装していた。NuttX ではファイルディスクリプタはタスクグループ単位でスコープされるため、`wg_rx_task`(ソケットの生成元タスクの子として `task_create()` される)からは問題なく使えるが、TCP の送信データを非同期に処理する **LPWORK システムワーカースレッド**(`wg_rx_task` と親子関係がない)からは同じ fd で `sendto()` すると `EBADF` になっていた。

ICMP echo reply や TCP の SYN-ACK は `wg_rx_task` 自身のコンテキストで同期的に送信されるため気づかれず、非同期にキューされる TCP アプリケーションデータ(telnetd の応答、HTTP レスポンスなど)だけが無言で失敗する、という分かりにくい壊れ方をしていた。

修正は、ファイルディスクリプタテーブルを経由しない NuttX 内部 API `psock_socket()` / `psock_bind()` / `psock_sendto()` / `psock_recvfrom()` / `psock_close()` への切り替え。`struct socket` はただのメモリ上の構造体なので、どのタスク・スレッドからポインタ経由で触っても問題ない。

## デモの再現方法

```bash
docker build --target esp32s3 -t nuttx-wireguard:esp32s3 .
```

Wi-Fi 認証情報・WireGuard 秘密鍵・ピア設定を `kconfig-tweak --set-str` で実際の値に上書きしてから書き込む(詳細は [docs/hardware-verification.md](hardware-verification.md))。上記ビルドには `CONFIG_NETUTILS_WEBSERVER` / `CONFIG_EXAMPLES_WEBSERVER` が有効化済みで、デモ用にブランディングした `docker/webserver-demo/` 以下のページが同梱される。

実機上で:

```
nsh> wg
nsh> wg show                # ハンドシェイク確認
nsh> webserver &            # Web サーバーをバックグラウンド起動
```

トンネルの反対側から:

```
telnet 10.10.0.2            # NSH にログイン
```

ブラウザで `http://10.10.0.2/` を開くとデモページが表示される。

## 現在地

プロポーザルの Phase 4(実機テスト)完了。詳細な進捗は [DEVELOPMENT.md](../DEVELOPMENT.md)、作業ログは [docs/phase4-log.md](phase4-log.md) を参照。
