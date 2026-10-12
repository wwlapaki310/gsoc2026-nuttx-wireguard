# Stack-chan WireGuard デモ

## 1. WireGuardの接続状態

```powershell
docker exec wg-peer wg show wg0
```

見せる場所：

- `latest handshake`：通信相手との接続が成立
- `transfer`：暗号化トンネルを通った送受信量

説明：

> PCとStack-chanの間でWireGuardトンネルが成立しています。

## 2. トンネル内IPへのPing

```powershell
docker exec wg-peer ping -c 4 -W 2 10.10.0.2
```

見せる場所：

```text
4 packets transmitted, 4 received, 0% packet loss
```

説明：

> `10.10.0.2` はStack-chanのWireGuardインターフェースです。

## 3. WireGuard経由のWeb UI

```powershell
Start-Process http://127.0.0.1:8080/
```

説明：

> このWeb画面はWireGuardトンネル経由でStack-chanから取得しています。

## 4. WireGuard経由でStack-chanを操作

```powershell
docker exec -it wg-peer telnet 10.10.0.2
```

Telnet接続後：

```text
ifconfig wlan0
ifconfig wg0
wg show
stackchan status
stackchan face happy
stackchan blink
stackchan led 0 16 32
stackchan servo ping
stackchan servo pos
stackchan say http://10.10.0.1:8000/tunnel.wav
stackchan led 0 0 0
exit
```

見せる内容：

- `wlan0`：通常のWi-Fiアドレス
- `wg0`：WireGuardの `10.10.0.2`
- 顔、まばたき、LED、サーボがネットワーク越しに動く
- 音声ファイルもトンネル経由で取得する

## 5. 暗号化の外側と内側を比較

PowerShellを3つ開く。

### 画面1：暗号化された外側

```powershell
docker exec -it wg-peer tcpdump -l -nn -q -i eth0 udp port 51820
```

見せる内容：

> 送信元、宛先、パケットサイズは見えますが、コマンドの内容は読めません。

### 画面2：復号された内側

```powershell
docker exec -it wg-peer sh -c "tcpdump -l -nn -A -s 0 -i wg0 'tcp port 23' 2>/dev/null | grep -aoE --line-buffered 'stackchan (face happy|blink)|face: happy|blink: ok|nsh>'"
```

### 画面3：比較用コマンドを送信

```powershell
docker exec wg-peer sh -c "(sleep 1; printf 'stackchan face happy\r\nstackchan blink\r\n'; sleep 3; printf 'exit\r\n') | nc 10.10.0.2 23"
```

画面2に表示される内容：

```text
stackchan face happy
stackchan blink
face: happy
blink: ok
```

説明：

> 外側では暗号化されたUDPにしか見えません。同じ通信をWireGuardが復号すると、
> 実際のコマンドと応答を確認できます。

画面1と画面2は `Ctrl+C` で終了する。
