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
