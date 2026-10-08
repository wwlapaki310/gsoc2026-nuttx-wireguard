"""Set up a WireGuard tunnel between the board and a Docker container peer.

    python wg_setup.py COM5 <board-wlan0-ip>

Peer side: a container from docker/wg-peer (kernel WireGuard in the Docker
Desktop VM, needs only NET_ADMIN), named "wg-peer", with wg0 = 10.10.0.1/24.
Board side: wg0 = 10.10.0.2 (CONFIG_NET_WIREGUARD_LOCAL_IPADDR).

The PC initiates. On a PC whose firewall drops unsolicited inbound traffic
(Public profile, no local rules allowed) the board cannot reach the peer
first, so the container gets the board's endpoint and a keepalive, and the
board's peer entry has no endpoint: it learns it from the first valid
handshake (WireGuard roaming). The keepalive keeps the outbound UDP flow
open so the board's replies get back in.

Keys are generated in the container and kept in %USERPROFILE%\\stackchan-wg\\
(never printed). On the board the configuration is written with
"wg saveconf" so /etc/init.d/rcS brings the tunnel up at boot. NSH echoes
the commands, so the board output is filtered: every key is shown as ***.

Port 8080 on localhost is forwarded through the tunnel to the board's web
server (http://localhost:8080/), and 2323 to its telnetd.
"""
import os
import subprocess
import sys
import time

import serial

IMAGE = 'nuttx-wireguard:wg-peer'
NAME = 'wg-peer'
KEYDIR = os.path.join(os.path.expanduser('~'), 'stackchan-wg')


def sh(*args, check=True, input=None):
    r = subprocess.run(args, capture_output=True, text=True, input=input)
    if check and r.returncode != 0:
        sys.exit('%s failed:\n%s%s' % (' '.join(args[:4]), r.stdout,
                                       r.stderr))
    return r.stdout.strip()


def key(name, gen):
    path = os.path.join(KEYDIR, name)
    if not os.path.exists(path):
        with open(path, 'w') as f:
            f.write(gen() + '\n')
    with open(path) as f:
        return f.read().strip()


def pubkey(priv):
    return sh('docker', 'run', '--rm', '-i', IMAGE, 'wg', 'pubkey',
              input=priv + '\n')


def nsh(s, cmd, secrets, wait=2.0):
    # The USB-Serial/JTAG console drops input when a long line arrives in
    # one burst (a 44-character key came through mangled), so trickle it.
    data = (cmd + '\r\n').encode()
    for i in range(0, len(data), 16):
        s.write(data[i:i + 16])
        s.flush()
        time.sleep(0.03)
    buf = b''
    t0 = time.time()
    while time.time() - t0 < wait:
        buf += s.read(4096)
    text = buf.decode('utf-8', 'replace').replace('\r', '')
    for k in secrets:
        text = text.replace(k, '***')
    sys.stdout.write(text)
    sys.stdout.flush()
    return text


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    port, board_ip = sys.argv[1], sys.argv[2]

    os.makedirs(KEYDIR, exist_ok=True)
    gen = lambda: sh('docker', 'run', '--rm', IMAGE, 'wg', 'genkey')
    pc_priv = key('pc.key', gen)
    sc_priv = key('stackchan.key', gen)
    pc_pub = pubkey(pc_priv)
    sc_pub = pubkey(sc_priv)
    secrets = [pc_priv, sc_priv, pc_pub, sc_pub]

    # Peer container (recreated each time so the config always matches)

    sh('docker', 'rm', '-f', NAME, check=False)
    sh('docker', 'run', '-d', '--name', NAME, '--cap-add', 'NET_ADMIN',
       '-p', '127.0.0.1:8080:8080', '-p', '127.0.0.1:2323:2323', IMAGE)
    sh('docker', 'exec', '-i', NAME, 'sh', '-c',
       'umask 077; cat > /etc/wireguard/pc.key', input=pc_priv + '\n')
    sh('docker', 'exec', NAME, 'sh', '-c',
       'ip link add wg0 type wireguard && '
       'wg set wg0 private-key /etc/wireguard/pc.key '
       'peer %s endpoint %s:51820 allowed-ips 10.10.0.2/32 '
       'persistent-keepalive 25 && '
       'ip addr add 10.10.0.1/24 dev wg0 && ip link set wg0 up && '
       '(socat TCP-LISTEN:8080,fork,reuseaddr TCP:10.10.0.2:80 & '
       ' socat TCP-LISTEN:2323,fork,reuseaddr TCP:10.10.0.2:23 &)'
       % (sc_pub, board_ip))
    print('peer container up: wg0 10.10.0.1 -> %s:51820' % board_ip)

    # Board

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    try:
        nsh(s, '', secrets, 1)
        nsh(s, 'wg down', secrets)
        nsh(s, 'wg set private-key %s' % sc_priv, secrets)
        nsh(s, 'wg set peer %s allowed-ips 10.10.0.1/32' % pc_pub, secrets)
        nsh(s, 'wg up', secrets, 3)
        nsh(s, 'wg saveconf', secrets, 3)
    finally:
        s.close()

    # Let the container's keepalive / handshake go through

    time.sleep(6)
    print(sh('docker', 'exec', NAME, 'sh', '-c',
             'wg show wg0 | grep -E "handshake|transfer"; '
             'ping -c 4 -W 2 10.10.0.2 | tail -2', check=False))


if __name__ == '__main__':
    main()
