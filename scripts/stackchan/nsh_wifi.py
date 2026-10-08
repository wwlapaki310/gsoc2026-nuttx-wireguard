"""Join a Wi-Fi network from NSH without the passphrase reaching any log.

    python nsh_wifi.py COM5 [--save]

Asks for the SSID and the passphrase (hidden), then runs on the board:

    wapi psk wlan0 <passphrase> 3
    wapi essid wlan0 <ssid> 1
    renew wlan0
    [wapi save_config wlan0]     # --save: reconnect by itself at boot
    ifconfig wlan0

NSH echoes what it receives, so every line of output is filtered and the
passphrase is replaced with ***. The port is opened with DTR/RTS low, as in
nsh_console.py (on USB-Serial/JTAG they drive reset and the boot strap).
"""
import getpass
import os
import sys
import time

import serial


def run(s, cmd, secret, wait):
    s.write((cmd + '\r\n').encode())
    buf = b''
    t0 = time.time()
    while time.time() - t0 < wait:
        buf += s.read(4096)
    text = buf.decode('utf-8', 'replace').replace('\r', '')
    if secret:
        text = text.replace(secret, '***')
    sys.stdout.write(text)
    sys.stdout.flush()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port = sys.argv[1]
    save = '--save' in sys.argv

    # WIFI_SSID / WIFI_PASS let a wrapper pass them in without a prompt
    ssid = os.environ.get('WIFI_SSID') or input('SSID: ').strip()
    psk = os.environ.get('WIFI_PASS') or getpass.getpass('Passphrase (hidden): ')
    if not ssid or not psk:
        sys.exit('SSID and passphrase are required')
    if any(c in ssid + psk for c in ' "\'\\'):
        sys.exit('spaces, quotes and backslashes are not supported by NSH '
                 'argument parsing here')

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    try:
        run(s, '', psk, 1)
        run(s, 'wapi psk wlan0 %s 3' % psk, psk, 2)
        run(s, 'wapi essid wlan0 %s 1' % ssid, psk, 6)
        run(s, 'renew wlan0', psk, 12)
        if save:
            run(s, 'wapi save_config wlan0', psk, 3)
        run(s, 'ifconfig wlan0', psk, 2)
    finally:
        s.close()
    print()


if __name__ == '__main__':
    main()
