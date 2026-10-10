# StackChan 実機確認(2026-10-08)

issue #20 の S0〜S5 に相当する確認。目的は「初期ファームでは顔も首振りも動いていたのに、NuttX では
サーボが動かない」原因の切り分け。**ハードウェアは正常で、原因は NuttX 側の I2C 設定**と判明した。

## 対象

| 項目 | 値 |
|---|---|
| 機体 | M5Stack StackChan(CoreS3 + 胴体)。USB は胴体側(底部)の USB-C に接続 |
| SoC | ESP32-S3 (QFN56) rev v0.2、16 MB Flash、MAC `7c:4f:ad:af:cf:0c` |
| 接続前に入っていたもの | NuttX 13.0.1 `cec617df-dirty`(2026-10-05 ビルド、`esp32s3-devkit` 構成、apps 版 WireGuard + `stackchan` アプリ)。ソースはこの環境に無い |
| バックアップ | 全 16 MB を `C:\Users\0000400096\stackchan-backup\flash-backup-20261008-nuttx-preexisting.bin` に保存(SHA-256 `f48a376decce6546036a38d2cffc2eb85de2f1d2cdb639f19cc39925631a7412`) |

## 1. 既存の NuttX での結果

| コマンド | 結果 |
|---|---|
| NSH | COM5(USB-Serial/JTAG、`/dev/ttyACM0`)。**ポートを開くとき DTR/RTS を触るとリセット・ブート選択に入る**ので、両方 False のまま開く |
| `stackchan face` | エラーなし。LCD を **240x320(縦)** と報告 |
| `stackchan power-on` | エラーなし(AW9523B / AXP2101 への書き込みは成功) |
| `stackchan move …` | **失敗**:`PY32 IO expander (0x6f) not responding after waiting up to 1200ms: -110` |
| `stackchan stop` | **NSH 全体が固まる**(ハードリセットで復旧)。サーボ応答をタイムアウトなしで待っているとみられる |
| `wg` | `/data` が空(`wg0.conf` なし)のため起動時 `wg0` 失敗(-22) |
| その他 | `i2c` ツール・`dmesg`(RAMLOG)なし |

## 2. MicroPython での切り分け(一時的に書き込み、確認後にバックアップを書き戻す)

公式 MicroPython `ESP32_GENERIC_S3-20260824-v1.29.0` を使用。

### I2C スキャン(SCL=G11, SDA=G12)

| 速度 | 応答したアドレス |
|---|---|
| **100 kHz** | 0x23, 0x34 (AXP2101), 0x40, 0x41 (INA226), 0x50, 0x51 (BM8563), 0x58 (AW9523B), 0x68 (Si12T), 0x69 (BMI270), **0x6F (PY32)** |
| **400 kHz** | 上と同じ、ただし **0x6F (PY32) だけ応答しない** |

→ **PY32 は 400 kHz では応答しない。** 公式 BSP が PY32 だけ `freq = 100000` を指定している理由と一致する。
既存 NuttX の症状(他のチップには書けるが PY32 だけ -110)はこれで説明できる。

### PY32(0x6F)

- バージョン 0x41、UID 0x84AD
- レジスタ:方向 0x03/0x04、出力 0x05/0x06、プルアップ 0x09/0x0A、駆動 0x13/0x14、LED 設定 0x24、LED RAM 0x30〜(RGB565、下位バイトが先)
- サーボ電源 VM_EN = ピン 0(出力・プルアップ・High)。LED = ピン 13(高位レジスタの bit 5、push-pull)
- LED:個数 12 を 0x24 に書き、RAM に色を書いて 0x24 の bit 6 で更新。更新後 bit 6 は自動で 0 に戻る(赤→緑→青→消灯を実行)

### 電源

- AXP2101 status0 = 0x38(VBUS あり、バッテリーあり)
- INA226(0x41)の bus 電圧 4.16 V は**バッテリー電圧**で、VM_EN の ON/OFF では変わらない(サーボ電源の監視には使えない)
- AW9523B P1 = 0x82(bit 7 = SY7088 BOOST_EN は既に 1、bit 1 = LCD RST High)
- **BUS_OUT_EN(AW9523B P0 bit 1)は触らない。** M5Unified は「バッテリーなしで外部 5V が BUS に来ている間は、有効化すると自身の給電を断つ」として取り消している

### サーボ(UART1、TX=G6 / RX=G7、1 Mbps、8N1、Feetech SCS)

- VM_EN ON 後 **500 ms 待ち**、応答待ち 25 ms で ping:ID 1・ID 2 が応答(status 0x00)。500 kbps・115200 では応答なし
- 最初の試行(VM_EN 後 300 ms、応答待ち 8 ms)では無応答だった。**電源投入後の待ち時間と応答待ちが短いと失敗する**
- 送信のエコーは RX に返らない(半二重の折り返しなし)
- 位置(addr 0x38、ビッグエンディアン)を読み、目標位置ブロック(addr 0x2A:位置・時間・速度)で移動:

| 指令 | 応答 | 読み戻し |
|---|---|---|
| 開始時 | — | pan 406 / tilt 671 |
| pan → 460 | ACK | 457 |
| tilt → 650 | ACK | 650 |
| pan → 420 | ACK | 424 |
| pan → 500 | ACK | 497 |
| pan → 460 | ACK | 464 |
| tilt → 690 | ACK | 689 |
| tilt → 650 | ACK | 651 |

  誤差 ±4 単位(約 ±1.3°)。最後にトルクを解放(addr 0x28 = 0)。安全範囲は BSP の値(pan 0 点 460・±128° → 51〜869、tilt 0 点 620〜90° → 620〜908)の内側に限定

### LCD

- 電源:AXP2101 0x90 bit 7(DLDO1 = バックライト)ON、0x99 = 24(M5GFX の明るさは 20〜28)
- 書き込み:SPI(SCK=G36、MOSI=G37、CS=G3、DC=G35)で SLPOUT → COLMOD 0x55 → MADCTL 0x08 → INVON → DISPON の後、黒背景に白い目 2 つと口を描画。**本人の目視で顔が表示されたことを確認**
- 読み出し(RDDID・C/E 判定・RAMRD)は、G35・G37 のどちらで読んでも全ビット 1 で**読めなかった**。ILI9342C / E の判別は未確定。顔の表示には不要

## 3. NuttX 側で直すこと

| # | 内容 | 根拠 |
|---|---|---|
| 1 | **I2C0 を PY32 とやり取りするときは 100 kHz にする**(NuttX は `struct i2c_msg_s` の `frequency` をメッセージ単位で指定できる) | §2 の I2C スキャン |
| 2 | VM_EN ON の後に 500 ms 程度待ってからサーボと通信する | §2 のサーボ |
| 3 | サーボ応答の受信にタイムアウトを付ける(`stackchan stop` の固まり) | §1 |
| 4 | LCD の向き:物理は 320x240 横長。現行は 240x320 縦として描画している | §1、M5GFX は `offset_rotation = 3` |
| 5 | `i2c` ツール(i2ctool)を入れる | 次回の切り分け用 |

## 4. 片付け(未完了)

**バックアップの書き戻しはまだできていない。** 実機は MicroPython が入ったままで、その USB(COM6)は
`machine.bootloader()` を試した後に止まっている(Windows がエラー 31 を返す)。esptool の自動リセット
(`default-reset` / `usb-reset`)も効かなかった。

戻すには、CoreS3 側面のリセットボタンを緑 LED が点くまで約 3 秒長押ししてダウンロードモードに入れ、
`python -m esptool --port <COM> --baud 921600 write-flash 0 <backup.bin>` を実行する。

- バックアップの実体は**確認に使った PC のローカル**(`C:\Users\0000400096\stackchan-backup\`)にだけある。
  ビルド済みイメージには Wi-Fi の設定が入っている可能性があるので、**公開リポジトリには入れない**
- 書き戻しても、PY32 を 400 kHz で叩く問題はビルドに含まれたままなので、サーボは動かない。
  §3 の修正を入れたファームを作り直すのが本筋

## 5. 次の作業(ビルドできる PC で)

目標:NuttX 上で顔表示・まばたき・首振りを続けながら、WireGuard 越しに telnet と Web で接続するデモ(#20 S6〜S7)。

1. `stackchan` アプリを §3 の手順で作る(元のソースはこの環境に無かった。scripts/stackchan/ の MicroPython 版が
   動作確認済みの手順の参照実装)。I2C は `struct i2c_msg_s.frequency = 100000`、サーボ UART は `/dev/ttyS1` 1 Mbps
2. ボードは esp32s3-devkit 構成を流用中(CoreS3 専用 board は NuttX にまだ無い)。コンソールは USB-Serial/JTAG
3. Wi-Fi の SSID・パスフレーズと WireGuard の鍵は**ビルドに入れない**。起動後に NSH(`wapi`、`wg genkey` / `wg set`)で設定する
4. WireGuard の相手(Linux か Windows の公式クライアント)と、UDP が通るネットワークを用意する
5. 確認順:NSH → 顔 → LED → サーボ → Wi-Fi → `wg0` → telnet / Web を、顔と首振りを動かしたまま

## 6. NuttX での再確認(同日夜、WSL + Docker Desktop で再ビルド)

§3 の 1〜5 を入れた `stackchan` アプリ([demo/stackchan/](../../demo/stackchan/))を作り、`esp32s3` ステージに
重ねる `esp32s3-stackchan` ステージ(Dockerfile)でビルドして COM5 に書き込んだ。§4 の「書き戻し」は
不要になった(書き込み前の実機は MicroPython ではなく、同日 12:22 ビルドの NuttX 13.0.1 が COM5 で動いていた)。

```bash
docker build --target esp32s3-stackchan -t nuttx-wireguard:esp32s3-stackchan .
docker run --rm -v "C:\Users\<user>\stackchan-build:/out" --entrypoint bash \
  nuttx-wireguard:esp32s3-stackchan -c 'cp /opt/nuttx/nuttx.bin /out/'
python -m esptool -c esp32s3 -p COM5 -b 921600 write-flash -fs detect -fm dio -ff 40m 0x0000 nuttx.bin
```

### 構成で踏んだこと

| 内容 | 対処 |
|---|---|
| `esp32s3-devkit:wifi` はコンソールが UART0(G43/G44)。CoreS3 の USB-C は内蔵 USB-Serial/JTAG なので、書き込みは通るのに COM5 に何も出ない | `ESP32S3_UART0` を外して `ESP32S3_USBSERIAL` をコンソールに(esp32s3-box:nsh と同じ) |
| UART0 を外すと UART1 は `/dev/ttyS0` になる(`/dev/ttyS1` ではない) | アプリの `CONFIG_EXAMPLES_STACKCHAN_SERVO_DEVPATH`(既定 `/dev/ttyS0`) |
| esp32s3-devkit の SPI3 用 `cmddata` は MISO ピンを DC として書くが、そのピンを出力にしていない | Dockerfile で、最初の呼び出し時に `esp_configgpio(MISO, OUTPUT)` するパッチを当てる |
| LED を更新した直後は PY32 が I2C に応答しない(デモの起動直後に `servo_power` が -EIO) | 更新後 50 ms 待つ。I2C 転送は 10 ms 間隔で最大 5 回再試行 |

### 結果(コマンドの戻り値と読み戻しで確認。画面と LED は目視が未確認)

| 確認 | 結果 |
|---|---|
| NSH | COM5(USB-Serial/JTAG)。`/dev/i2c0`・`/dev/spi3`・`/dev/ttyS0`・`/dev/ttyACM0` あり |
| `i2c dev -b 0 0x08 0x77` | 0x23, 0x34, 0x40, 0x41, 0x50, 0x51, 0x58, 0x68, 0x69, **0x6F**(i2ctool の既定 100 kHz) |
| `stackchan face` / `blink` / `led r g b` | すべて ok(I2C・SPI の転送エラーなし) |
| `stackchan servo ping` | ID 1・2 とも応答 |
| `stackchan servo move 440 650` → `pos` | pan 442 / tilt 650 |
| `stackchan servo move 480 670` → `pos` | pan 477 / tilt 668 |
| `stackchan start`(バックグラウンド) | 約 10 秒間に pan/tilt の読み戻しが 452/649 → 471/632 → 467/680 と変化。NSH は応答し続ける。`free` は約 200 KB 空き |
| `stackchan stop` | 中央に戻してトルク解放、LED 消灯、タスク終了(`status` → stopped)。NSH は固まらない |

### S6〜S7:WireGuard 越しの telnet / Web(顔と首振りを動かしたまま)

**動作した。** 電源投入だけで Wi-Fi → `wg0` → webserver → `stackchan start` → telnetd の順に上がり
(rcS と `/data` の保存設定)、PC 側からトンネル越しに telnet と Web がつながる。顔(白い目と口)は目視で確認済み。

構成:

| 側 | 内容 |
|---|---|
| StackChan | `wlan0` 192.168.0.184(TP-Link_5FB2、DHCP)、`wg0` 10.10.0.2。`wg saveconf` → `/data/wg0.conf`。Wi-Fi のパスフレーズは平文になるので保存しない(確認後に `/data/wapi.conf` を削除)。電源を入れ直したら USB から `nsh_wifi.py` でつなぎ直す |
| PC | Docker Desktop のコンテナ([docker/wg-peer/](../../docker/wg-peer/))でカーネル版 WireGuard、`wg0` 10.10.0.1。`localhost:8080` → 10.10.0.2:80、`localhost:2323` → 10.10.0.2:23 を socat で転送 |
| 向き | **PC 側から張る。** PC は Windows ファイアウォールが Public プロファイルで受信を拒否し、ローカルルールも追加できない。コンテナ側に StackChan のエンドポイントと keepalive 25 秒を設定し、StackChan 側のピアはエンドポイントなし(最初の正しいハンドシェイクで学習する) |

手順([scripts/stackchan/](../../scripts/stackchan/)):

```
python nsh_wifi.py COM5                     # SSID とパスフレーズを入力(出力では伏せる)。--save で /data/wapi.conf に平文保存
python wg_setup.py COM5 <StackChan の wlan0 IP>  # 鍵の生成・両側の設定・保存。鍵は %USERPROFILE%\stackchan-wg\
```

| 確認 | 結果 |
|---|---|
| ハンドシェイク | 成立(PC 側から) |
| `ping 10.10.0.2`(コンテナから) | 10/10、RTT 9〜28 ms |
| telnet(トンネル越し) | `uname -a`・`ps`・`stackchan status` / `servo pos` が通る。デモ実行中も首の位置が変わり続ける(405/661 → 448/680 → 496/674) |
| Web(トンネル越し) | 1 秒間隔で 30/30、`localhost:8080` 経由でも 15/15(TIME_WAIT 2 秒の構成) |
| 再起動 | リセット後、操作なしでトンネル・webserver・デモ・telnetd が上がる |

### 表情の切り替え

`stackchan face <neutral|happy|sad|angry|sleepy|surprised>` で目と口の形を変えられる(目視で確認)。デモの実行中は
値を渡すだけで、描画はデモのタスクが行う(2 つのタスクが同時に SPI に描かないため)。まばたきも今の表情のまま行う。
telnet からトンネル越しに `stackchan face happy` で切り替わることも確認した。

### 途中で踏んだこと(S6〜S7)

| 内容 | 対処 |
|---|---|
| `/data` に保存したはずの `wg0.conf` / `wapi.conf` が再起動後に見えない(`df` では使用中、`ls` は空) | 書き込み前に入っていたファームと SPIFFS の形式が合っていなかった。`esptool erase-region 0x180000 0x100000` で消してから設定し直した |
| `wapi.conf` で Wi-Fi にはつながるが、アドレスが既定の 10.0.0.2 / 0.0.0.0 のまま | `esp32s3` ステージは SSID 指定時しか `NETINIT_DHCPC` を入れない。stackchan ステージで常に有効化 |
| NSH に 44 文字の鍵を含む行を一度に送ると、USB-Serial/JTAG で文字が落ちて鍵が壊れる | `wg_setup.py` は 16 バイトずつ 30 ms 間隔で送る |
| telnet セッションから `webserver &` を起動すると、セッションを閉じた後に webserver が落ちる | rcS(コンソール)から起動する |
| `nc -zv` のように接続してすぐ切ると telnetd / webserver が終了する | NuttX apps 側の `accept()` エラー処理(エラーでループを抜ける)。デモでは避ける |
| Web が数回成功した後、しばらく RST になる。handshake は成立し、GET の再送に対して RST(ボードがその接続を失っている) | TCP の接続数を 16 + 動的 32、TIME_WAIT を 2 秒にして、1 秒間隔なら安定。**連続で叩くと 30 回中 12 回しか成功せず、まだ接続を取り切っているものがある**。根本原因は未調査(NuttX TCP 側。成功回数が接続数に比例し、TIME_WAIT 満了で回復する)。SYN-ACK の ISN が接続ごとにほぼ同じ値なのも気になる |
| Docker Desktop が社内の PAC プロキシを覚えたままで、社外でビルドが失敗 | Docker Desktop を再起動 |

### 残り

- **胴体の LED が光らない。**(「白く見える」は画面の顔のことだった)赤・緑・青・消灯を 1 秒ずつ繰り返しても変化なし。
  PY32 の LED RAM には正しく入っている(赤で 0x30/0x31 = `00 f8`、0x24 = `0c` で更新ビットは PY32 がクリア)。
  ピン 13 の設定(0x04 = 0x20、0x0A = 0x20、0x14 の bit5 = 0)も公式 BSP(StackChan-BSP の `PY32IOExpander`)と同じ。
  BSP は先に `M5.begin()`(M5Unified)を呼ぶので、CoreS3 の外部 5V 出力(AW9523B / AXP2101 側)が LED の電源で、
  NuttX では入っていない可能性が高い。次はそこを確かめる
- Web が連続アクセスで落ちる件の根本原因(上表)
- `docker/wg-peer` を使わず、Windows の公式 WireGuard クライアントを相手にする場合は管理者権限のある PC が要る

## 7. 首振りが止まった件(2026-10-10、Glasgow 会場で)

**原因は NuttX の ESP32-S3 I2C ドライバのタイムアウトが短すぎたこと。** 筐体の故障ではなかった。

| 確認 | 結果 |
|---|---|
| 症状 | `PY32 (0x6f) not answering: -5`。アドレスには ACK するが、レジスタの読み書きが全部失敗し、レジスタ指定なしで読むと常に 0x00 |
| 電源の完全オフ(左のボタン 6 秒)→ 起動 | 変わらず |
| 全バスを 100 kHz にし、起動時に BSP と同じく版数を待つ | 変わらず(400 kHz 説は外れ) |
| **同じ状態のまま MicroPython を書き込む(PY32 は電源を切っていない)** | **版数 0x41 が普通に読めた** |

PY32 はマイコンなので、返答の準備中に SCL を引き延ばす(clock stretching)。`esp32s3_i2c.c` はハードウェアの
タイムアウトを「約 10 バスサイクル」(100 kHz で約 0.1 ms)に設定しており、引き延ばしがそれより長いと毎回
タイムアウトになる。MicroPython / ESP-IDF の既定は 50 ms。PY32 の引き延ばしは状態によって長さが変わるらしく、
日本では動いていた。stackchan ステージで、このタイムアウトを 2^21 クロック(40 MHz で約 52 ms)にするパッチを
当てて解決した(upstream に出す価値のある修正)。

あわせて直したこと:

- rcS で `wg` が失敗すると(`/data/wg0.conf` が無いとき -22)、NSH はそこでスクリプトを止めるので、後ろに
  置いた webserver と `stackchan start` が走らなかった。この 2 行を `wg` の前に移した
- PY32 が応答しないときのエラーは 1 回だけ表示し、デモの再試行は 10 秒おきにした。`stackchan py32` で版数を読める
- i2ctool は既定が 400 kHz なので PY32 が見えない。また `-w 16` などのオプションは次の呼び出しにも残る。
  PY32 を見るときは `-w 8 -f 100000` を明示する

MicroPython から NuttX に戻すには: `machine.bootloader()` の後は USB が止まるので、**胴体底のボタンを 3 秒押して
(オレンジ点滅)ダウンロードモード**にして書き込み、最後に**同じボタンを短く 1 回**押して抜ける。

### 会場のネットワーク

会場の Wi-Fi(Apache)にはつながる(2.4 GHz、WPA2 パスフレーズ)が、**端末どうしの通信が遮断されていて**、
PC からスタックちゃんへは ARP も通らない。スマホのテザリングに PC とスタックちゃんの両方をつなげば、家と同じ
構成(PC 側の Docker コンテナから張るトンネル)で動く。日本語の SSID でも `nsh_wifi.py` で問題なくつながった。

## 8. しゃべらせる(2026-10-10)

`stackchan say <file|http://url> [rate]` で WAV を再生し、再生中は口を動かす。音声は PC の Windows 音声合成で作り
([scripts/stackchan/make_voice.py](../../scripts/stackchan/make_voice.py)、Zira・高めの声)、WireGuard の相手の
コンテナ(10.10.0.1:8000)から HTTP で配る。外部の API やクラウドは使っていない。

構成: CoreS3 のスピーカーアンプ AW88298(I2C 0x36)を I2S1(BCK=G34、WS=G33、DOUT=G13)で鳴らす。
アンプのリセット解除は AW9523B P0.2、初期化値は M5Unified の CoreS3 用コールバックと同じ。NuttX 側は汎用 I2S
オーディオ(`/dev/audio/pcm1`)+ PCM デコーダ + nxplayer(HTTP ストリーミング)。

踏んだこと:

| 内容 | 対処 |
|---|---|
| 再生開始から約 0.5 秒で全体が固まる(USB コンソールもネットワークも止まる) | **NuttX の `esp_i2s.c` のバグ。** TX 完了割り込みで最後の DMA ディスクリプタを探すループが `bfdesc_ctrl` を更新せず、1 バッファが複数ディスクリプタにまたがると割り込み内で無限ループになる(RX 側は修正済みだった)。Dockerfile でパッチ。upstream に出す価値あり |
| `pcm_enqueuebuffer: ERROR: Invalid PCM WAV file` の後、nxplayer が 0 バッファのまま「再生中」で止まる | Windows 音声合成の WAV は fmt チャンクが 18 バイト。16 バイトに書き直す(`make_voice.py`) |
| 声が倍速・高音になる | モノラルが I2S でステレオとして流れる。ステレオで作る |
| 口が動かない・語尾が切れる | nxplayer は再生開始後に play スレッドの中で状態を「再生中」にするので、それを待ってから終わりを待つ。終わった後 0.8 秒待ってアンプを切る。WAV の末尾にも 0.5 秒の無音を付けた |
| デバッグログが USB コンソールに出ない | syslog の既定の出力先(up_putc)は UART0 で、USB-Serial/JTAG には出ない。調べるときは RAMLOG にして `dmesg` で読む |

再生は 20 回強のうち 1 回だけ途中で止まったことがある(I2S 修正後)。原因は未特定。メモリリークは無い。

## 9. テザリングを別のスマホに替えた(2026-10-10)

SSID とパスフレーズは同じ。アドレス帯が 10.176.112.0/24 から 10.252.156.0/24 に変わった(ボード .2、PC .89)。
`nsh_wifi.py` と `wg_setup.py` をやり直すだけで、トンネル(ping 4/4)と `say` が通った。スマホのテザリングは
入れ直すとアドレス帯が変わることがあるので、スライドの構成図から wlan 側の具体的なアドレスを外した。

切り替え直後の 1 回目の `say` で、§8 の「途中で止まる」が再現した。

- ボードは音声を約 430 KB 受け取ったところで止まり、`wg show` の受信バイト数がそこで止まった
- PC からボードの wlan0 への ping も通らなくなり、USB コンソールも `ping` の途中から応答しなくなった
- 直前の ping は 2/3 で、電波が弱かった可能性がある
- 電源の入れ直しで復旧し、その後の `say` は成功した

ネットワーク側(HTTP 受信中のソケットか Wi-Fi ドライバ)で止まり、それに巻き込まれてコンソールも止まったように見える。
I2S の修正前の固まり方(再生開始 0.5 秒)とは別。原因は未特定のまま。デモでは止まったら電源を入れ直す手順にした。
