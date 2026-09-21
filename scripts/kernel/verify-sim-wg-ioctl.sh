#!/usr/bin/env bash
# TF: ioctl validation. Drive the wg command through invalid operations and
# check each is rejected and leaves wg0 unchanged. No peer or tap is needed
# -- this exercises the driver's SIOCSWGIF/SIOCSWGPEER validation only, so
# it is the fastest of the sim tests.
#
# Covers (plan §3, TF): up with no key; all-zero peer key; the interface's
# own key as a peer; a bad endpoint; allowed-ips overlap; the peer limit;
# cidr > 32; a malformed base64 private key. Each must fail without adding a
# peer or changing the interface.
set -euo pipefail

cd /opt/nuttx

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

max_peers="$(sed -n 's/^CONFIG_NET_WIREGUARD_MAX_PEERS=//p' .config)"
max_peers="${max_peers:-4}"
echo "MAX_PEERS = ${max_peers}"

# A pool of valid, distinct public keys for peers.
pubs=()
for _ in $(seq 1 $((max_peers + 2))); do
  pubs+=("$(wg genkey | wg pubkey)")
done

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  set +e
  printf "poweroff\n" >&3 2>/dev/null
  sleep 1
  kill "${nuttx_pid}" 2>/dev/null
  wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}
trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2

send() { printf "%s\n" "$1" >&3; sleep 0.6; }

# Run "wg show" and count the configured peers ("peer:" lines).
peer_count() {
  local before
  before="$(wc -l </tmp/nuttx.out)"
  printf "wg show\n" >&3
  sleep 1
  tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g' | grep -c "^peer:" || true
}

fail() { echo "FAIL: $1"; sed -n "1,200p" /tmp/nuttx.out; exit 1; }

# --- 0. up before any key is refused -----------------------------------
before="$(wc -l </tmp/nuttx.out)"
send "wg up"
if ! tail -n +"$((before + 1))" /tmp/nuttx.out | grep -qi "no private key"; then
  fail "up with no key was not refused"
fi
echo "PASS: up with no key refused"

# --- baseline: a key and one valid peer --------------------------------
send "wg genkey"
nuttx_priv="$(sed -n 's/\x1b\[K//g;p' /tmp/nuttx.out | grep -Eo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${nuttx_priv}" ] || fail "wg genkey produced no key"
nuttx_pub="$(printf "%s" "${nuttx_priv}" | wg pubkey)"
send "wg set private-key ${nuttx_priv}"
send "wg set address 10.10.0.2/24"
send "wg set peer ${pubs[0]} allowed-ips 10.10.0.101/32"
[ "$(peer_count)" -eq 1 ] || fail "baseline peer was not added"
echo "PASS: baseline (key + 1 peer)"

# helper: an operation that must fail and leave the peer count at $1
must_reject() {
  local want="$1"; shift
  send "$*"
  local n; n="$(peer_count)"
  [ "${n}" -eq "${want}" ] || fail "'$*' changed peers to ${n} (want ${want})"
}

# --- 1. all-zero peer key ---------------------------------------------
must_reject 1 "wg set peer AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA= allowed-ips 10.10.0.111/32"
echo "PASS: all-zero peer key rejected"

# --- 2. the interface's own key as a peer -----------------------------
must_reject 1 "wg set peer ${nuttx_pub} allowed-ips 10.10.0.112/32"
echo "PASS: self public key rejected"

# --- 3. overlapping allowed-ips (same /32 as the baseline peer) --------
must_reject 1 "wg set peer ${pubs[1]} allowed-ips 10.10.0.101/32"
echo "PASS: overlapping allowed-ips rejected"

# --- 4. bad endpoint (port 0) -----------------------------------------
must_reject 1 "wg set peer ${pubs[1]} endpoint 10.0.0.1:0 allowed-ips 10.10.0.113/32"
echo "PASS: bad endpoint rejected"

# --- 5. cidr > 32 ------------------------------------------------------
must_reject 1 "wg set peer ${pubs[1]} allowed-ips 10.10.0.114/33"
echo "PASS: cidr > 32 rejected"

# --- 6. peer limit -----------------------------------------------------
# Fill up to MAX_PEERS with distinct, non-overlapping peers, then one more.
n=1
for i in $(seq 1 $((max_peers - 1))); do
  send "wg set peer ${pubs[$i]} allowed-ips 10.10.$((i)).0/24"
  n=$((n + 1))
  [ "$(peer_count)" -eq "${n}" ] || fail "peer ${i} was not added"
done
echo "PASS: filled to MAX_PEERS (${max_peers})"
must_reject "${max_peers}" "wg set peer ${pubs[${max_peers}]} allowed-ips 10.10.200.0/24"
echo "PASS: peer past MAX_PEERS rejected"

# --- 7. malformed base64 private key (command-level) -------------------
before="$(wc -l </tmp/nuttx.out)"
send "wg set private-key not_a_valid_key"
if ! tail -n +"$((before + 1))" /tmp/nuttx.out | grep -qi "valid base64\|not a valid"; then
  fail "malformed private key was not rejected"
fi
echo "PASS: malformed private key rejected"

echo "PASS: sim WireGuard ioctl validation verified (TF)"
