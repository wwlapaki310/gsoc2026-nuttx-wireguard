#!/usr/bin/env bash
# StackChan demo over WireGuard, driven from the laptop.
#
#   ./demo.sh show      # wg show: handshake, endpoint, bytes (no keys)
#   ./demo.sh login     # telnet to the StackChan through the tunnel
#   ./demo.sh outside   # packets on the phone's network: only UDP 51820
#   ./demo.sh inside    # packets inside the tunnel: the telnet text
#   ./demo.sh say NAME  # make it speak NAME.wav without logging in
#
# Prepare once: phone hotspot on, laptop on it, Docker Desktop running,
# "python nsh_wifi.py COM5" and "python wg_setup.py COM5 <board IP>".
# Then use three terminals: login (left), outside and inside (right).
#
# Inside the telnet session:
#   ifconfig                                   wlan0 and wg0
#   stackchan face happy                       neutral happy sad angry ...
#   stackchan say http://10.10.0.1:8000/hello.wav
#   exit

set -eu
export MSYS_NO_PATHCONV=1          # Git Bash: keep /paths for docker exec
PEER=wg-peer
BOARD=10.10.0.2

case "${1:-}" in
  show)
    # Public information only: no private key is printed by "wg show"
    docker exec "$PEER" wg show wg0 | grep -vE "private key"
    ;;
  login)
    docker exec -it "$PEER" telnet "$BOARD"
    ;;
  outside)
    echo "== outside the tunnel: the phone's network sees only this =="
    docker exec -it "$PEER" tcpdump -l -n -i eth0 udp port 51820
    ;;
  inside)
    echo "== inside the tunnel (wg0): the same session in plain text =="
    docker exec -it "$PEER" tcpdump -l -n -A -i wg0 tcp port 23
    ;;
  say)
    name="${2:?usage: demo.sh say NAME}"
    docker exec "$PEER" sh -c "(sleep 3; printf 'stackchan say http://10.10.0.1:8000/$name.wav\r\n'; sleep 12; printf 'exit\r\n'; sleep 1) | nc $BOARD 23" \
      | tr -d '\r' | grep -a "say:" || true
    ;;
  *)
    sed -n '2,19p' "$0"
    exit 1
    ;;
esac
