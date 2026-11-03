---
name: spresense-build-flash
description: Build a NuttX image for Sony Spresense in Docker, then flash it to the real board from Windows with flash_writer.exe. Use when asked to build/flash/write Spresense firmware (nuttx.spk), set up the Spresense environment, or check the board over COM.
---

# Spresense: build in Docker, flash from Windows

Build happens in Docker; flashing happens on the Windows host. A Docker Desktop (WSL2) container cannot reach the USB serial port, so do not try to flash from inside the container.

## 0. Preconditions (check first)

- Board connected: `Get-PnpDevice -PresentOnly | ? FriendlyName -match 'CP210'` should show `Silicon Labs CP210x ... (COMn)`. Use that COM number below (COM6 on this PC).
- Flash tool: `.tmp_flash_tools\flash_writer.exe` in this repo (gitignored scratch). If missing, take `sdk/tools/windows/flash_writer.exe` from `sonydevworld/spresense`.
- CP210x driver installed (the COM port appears when the board is plugged in).

## 1. Build in Docker

Targets in `Dockerfile`: `spresense` (spresense:nsh, bare board) and `spresense-wifi` (spresense:wifi + iS110B Wi-Fi add-on, WireGuard Kconfig).

```bash
docker build --target spresense-wifi -t nuttx-wireguard:spresense-wifi .
# optional: --build-arg NUTTX_REF=master | nuttx-12.7.0   (default nuttx-13.0.1)
# headless Wi-Fi: --build-arg WIFI_SSID=... --build-arg WIFI_PASS=...  (never commit these)
```

Extract the image:

```bash
docker create --name x nuttx-wireguard:spresense-wifi
docker cp x:/opt/nuttx/nuttx.spk hw-images/<descriptive-name>.spk
docker rm x
```

Keep images in `hw-images/` with a name that says what is in them.

## 2. Flash from Windows (PowerShell)

```powershell
.tmp_flash_tools\flash_writer.exe -s -c COM6 -d -b 115200 hw-images\<name>.spk
```

- `-b 115200` is the safe default. 480 KB+ images (NuttX 13.0.1 / master) often fail at 921600 with `Not ACK, Not NAK`. Try 921600 only for small images.
- The board reboots by itself after writing.
- Close any terminal holding the COM port before flashing.

## 3. Verify

Use a terminal that stays open (Tera Term, or `python -m serial.tools.miniterm COM6 115200`). Opening the port with pyserial toggles DTR and resets the board, so every re-open reboots it.

Expected boot output includes `cxd56_farapiinitialize: Mismatched version: loader(20585) != Self(20596)`. This is a harmless warning (old GNSS loader FW); it does not affect Wi-Fi, WireGuard or general use.

## Gotchas

- Docker does not flash. Past flashes in this repo were all `flash_writer.exe` on the host; the container only builds.
- iS110B hardware version (v1.0A/B/C) is baked in by `CONFIG_WIFI_BOARD_IS110B_HARDWARE_VERSION_10{A,B,C}`; the Dockerfile targets v1.0C.
- Details and history: `docs/development/hardware-verification.md` (Spresense section), `DEVELOPMENT.md`.
