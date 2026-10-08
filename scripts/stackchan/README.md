# StackChan 実機確認スクリプト

[docs/development/stackchan-hardware-check-2026-10-08.md](../../docs/development/stackchan-hardware-check-2026-10-08.md)
の確認に使ったもの。ホスト側は Python 3 + `pyserial` + `esptool`(`python -m pip install --user esptool pyserial`)。

## NuttX 用

| ファイル | 用途 |
|---|---|
| `nsh_console.py` | NSH にコマンドを流して出力を取る。**DTR/RTS を False のまま開く**(USB-Serial/JTAG では両線がリセットとブート選択につながっている)。例: `python nsh_console.py COM5 20 "uname -a" "stackchan face"` |
| `nsh_wifi.py` | Wi-Fi に接続(`wapi psk` / `essid` / `renew`、`--save` で `/data/wapi.conf` に保存。ただしパスフレーズが平文で残るので、通常は付けない)。SSID とパスフレーズは入力させ、出力では伏せる。`WIFI_SSID` / `WIFI_PASS` 環境変数でも渡せる |
| `wg_setup.py` | WireGuard の鍵を作り、PC 側([docker/wg-peer/](../../docker/wg-peer/) のコンテナ)と StackChan 側を設定して `wg saveconf` まで行う。鍵は `%USERPROFILE%\stackchan-wg\` に置き、表示しない。例: `python wg_setup.py COM5 192.168.0.184` |

秘密鍵や SSID を表示するコマンド(`wg showconf`、設定ファイルの `cat`、`wapi show` の ESSID)は流さないこと。

## MicroPython 用(ハードウェアの切り分け)

公式 `ESP32_GENERIC_S3`(確認時は v1.29.0)を書いた状態で使う。`mpy_run.py` が raw REPL で各スクリプトを送る:
`python mpy_run.py COM6 mpy_i2c_scan.py`

| ファイル | 確かめること |
|---|---|
| `mpy_i2c_scan.py` | I2C0(SCL=11, SDA=12)を 100 kHz と 400 kHz でスキャン。**PY32(0x6F)は 100 kHz でしか見えない** |
| `mpy_power_status.py` | AXP2101 の VBUS/バッテリー、AW9523B のポート、INA226(0x41)の電圧。VM_EN の前後を比較 |
| `mpy_servo_scan.py` | VM_EN を ON にして 500 ms 待ち、1 Mbps / 500 kbps / 115200 でサーボ ID 0〜20 と 0xFE に ping |
| `mpy_servo_move.py` | 位置を読み、BSP の安全範囲内で小さく動かして読み戻し、最後にトルク解放 |
| `mpy_led.py` | PY32 経由で WS2812 ×12 を 赤→緑→青→消灯 |
| `mpy_lcd_face.py` | バックライト ON、最小の初期化、黒地に白い目と口。**後半の読み出し(ID・C/E 判定・画素)は全ビット 1 で機能しない**。表示は目視で確認済み |

## MicroPython から NuttX に戻す

MicroPython の USB(COM6、USB-OTG)からは esptool の自動リセットが効かない。`machine.bootloader()` も
USB が止まるだけだった。**CoreS3 側面のリセットボタンを、内部の緑 LED が点くまで約 3 秒長押し**して
ダウンロードモードに入れてから書く(ポートは USB-Serial/JTAG 側に戻る):

```
python -m esptool --port <COM> --baud 921600 write-flash 0 <backup.bin>
```
