#!/usr/bin/env bash
# The one resource path nothing else here reaches: an exhausted IOB pool.
#
# Every other test runs with a pool that never goes below full, so throttling,
# the wait path and recovery from exhaustion are all untested -- and "the pool
# stayed full" is not evidence about what happens when it does not. The driver
# holds a netpkt_queue_t, so this is exactly where a mishandled allocation
# failure would turn into a lost queue entry or a stuck interface.
#
# So shrink the pool and flood it. CONFIG_IOB_NBUFFERS drops to 12 (with
# IOB_BUFSIZE 196 that is about one full-size packet in flight, plus a little)
# and IOB_THROTTLE rises to 2, so allocations for non-critical paths start
# failing well before the pool is empty.
#
# 12 rather than 8: at 8 the handshake itself is marginal -- one run came up and
# the next did not -- so 8 measures unusability, not behaviour under
# exhaustion. That is a useful data point in its own right and is recorded, but
# it is not what this test is for.
#
# The counters in /proc/iobinfo are instantaneous, not cumulative, and one of
# them is easy to misread: nfree is the current free count, nwait is how many
# tasks are blocked *right now*, and nthrottle is how many a throttled caller
# may still take -- so a large nthrottle means the pool is healthy, not that
# throttling happened. There is no event counter to read afterwards, so the
# pool is sampled repeatedly *during* the load and the extremes are kept.
#
# This reconfigures and rebuilds, then restores the configuration and rebuilds
# again, so it costs several minutes and leaves the tree as it found it.
#
# Pass = the pool was genuinely driven down (a sample with nfree at 0, or a
# blocked task), the sim does not assert or die, the pool returns to full at
# rest, and the tunnel works again.
set -euo pipefail

cd /opt/nuttx

SMALL_NBUFFERS="${1:-12}"
SMALL_THROTTLE="${2:-2}"

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

cp .config /tmp/iob-config-backup
restore() {
  set +e
  echo "== restoring the configuration and rebuilding =="
  cp /tmp/iob-config-backup .config
  make olddefconfig >/dev/null 2>&1
  make -j"$(nproc)" >/tmp/iob-restore-build.log 2>&1 ||
    echo "WARNING: the restore build failed; see /tmp/iob-restore-build.log"
}

echo "== shrinking the pool to ${SMALL_NBUFFERS} buffers,"\
" throttle ${SMALL_THROTTLE} =="
kconfig-tweak --set-val CONFIG_IOB_NBUFFERS "${SMALL_NBUFFERS}"
kconfig-tweak --set-val CONFIG_IOB_NCHAINS "${SMALL_NBUFFERS}"
kconfig-tweak --set-val CONFIG_IOB_THROTTLE "${SMALL_THROTTLE}"
make olddefconfig >/dev/null 2>&1
grep -E "^CONFIG_IOB_(NBUFFERS|NCHAINS|THROTTLE|BUFSIZE)=" .config
make -j"$(nproc)" >/tmp/iob-build.log 2>&1 ||
  { echo "FAIL: build with a small pool"; tail -20 /tmp/iob-build.log;
    restore; exit 1; }

linux_priv="$(wg genkey)"; linux_pub="$(printf %s "${linux_priv}" | wg pubkey)"
printf %s "${linux_priv}" > /tmp/linux.key

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  # Keep the real exit status: the restore build below would otherwise
  # overwrite a failure with its own success, and the script would report FAIL
  # while exiting 0.
  local status=$?
  set +e
  ip link del wgtest0 2>/dev/null
  printf "poweroff\n" >&3 2>/dev/null; sleep 1
  kill "${nuttx_pid}" 2>/dev/null; wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
  restore
  exit "${status}"
}
trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2
send() { printf "%s\n" "$1" >&3; sleep "${2:-0.6}"; }
fail() { echo "FAIL: $1"; tail -40 /tmp/nuttx.out; exit 1; }
diagnostics() {
  sed 's/\x1b\[[0-9;]*[[:alpha:]]//g; s/\r//g' /tmp/nuttx.out
}

# "ntotal nfree nwait nthrottle" as four numbers on one line. NSH ends lines
# with CR; without stripping it the anchored pattern never matches and the
# sample silently comes back empty, which reads like a passing check.
iobinfo() {
  local before
  before="$(wc -l </tmp/nuttx.out)"
  send "cat /proc/iobinfo" 0.8
  tail -n +"$((before + 1))" /tmp/nuttx.out | sed 's/\x1b\[K//g; s/\r//g' |
    sed -n 's/^ *\([0-9][0-9]*\)  *\([0-9][0-9]*\)  *\([0-9][0-9]*\)  *\([0-9][0-9]*\) *$/\1 \2 \3 \4/p' |
    head -1
}

ip link show tap0 >/dev/null 2>&1 || fail "tap0 not created"
ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
ip link set tap0 up

send "wg genkey"
npriv="$(sed 's/\x1b\[K//g' /tmp/nuttx.out | grep -Eo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${npriv}" ] || fail "no key from genkey"
NPUB="$(printf %s "${npriv}" | wg pubkey)"
send "wg set private-key ${npriv}"
send "wg set address 10.10.0.2/24"
send "wg set peer ${linux_pub} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 5"
send "wg up"

ip link add wgtest0 type wireguard
wg set wgtest0 private-key /tmp/linux.key listen-port 51821 \
  peer "${NPUB}" allowed-ips 10.10.0.2/32 endpoint 10.0.0.2:51820 \
  persistent-keepalive 5
ip addr add 10.10.0.1/24 dev wgtest0; ip link set wgtest0 up

up=false
for _ in $(seq 1 15); do
  sleep 2
  if ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1; then up=true; break; fi
done
[ "${up}" = true ] || fail "the tunnel never came up with a small pool"
echo "PASS: the tunnel comes up with only ${SMALL_NBUFFERS} buffers"

start="$(iobinfo)"
echo "iobinfo at rest: ${start}"

# Two pressures at once: traffic through the tunnel, and a flood of forged
# initiations straight at the listen port, so the transport and handshake paths
# are both allocating while the pool is short.
python3 - "${NPUB}" <<'PY' &
import socket, os, hashlib, base64, sys, time
pub = base64.b64decode(sys.argv[1])
mac1_key = hashlib.blake2s(b"mac1----" + pub, digest_size=32).digest()

def initiation():
    body = (b"\x01\x00\x00\x00" + os.urandom(4) + os.urandom(32) +
            os.urandom(48) + os.urandom(28))
    mac1 = hashlib.blake2s(body, key=mac1_key, digest_size=16).digest()
    return body + mac1 + b"\x00" * 16

u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.bind(("10.0.0.1", 40007))
end = time.time() + 60
while time.time() < end:
    for _ in range(60):
        try:
            u.sendto(initiation(), ("10.0.0.2", 51820))
        except OSError:
            pass
    time.sleep(0.005)
PY
flood=$!

# Small packets at a high rate, and full-size ones that each need most of the
# pool to themselves (IOB_BUFSIZE is 196).
ping -f -s 200 -c 20000 -W 1 10.10.0.2 >/dev/null 2>&1 &
small=$!
ping -f -s 1400 -c 6000 -W 1 10.10.0.2 >/dev/null 2>&1 &
large=$!

min_free=""
max_wait=0
samples=0
for _ in $(seq 1 24); do
  row="$(iobinfo)"
  [ -n "${row}" ] || continue
  samples=$((samples + 1))
  free="$(echo "${row}" | cut -d' ' -f2)"
  waiting="$(echo "${row}" | cut -d' ' -f3)"
  if [ -z "${min_free}" ] || [ "${free}" -lt "${min_free}" ]; then
    min_free="${free}"
  fi
  if [ "${waiting}" -gt "${max_wait}" ]; then max_wait="${waiting}"; fi
done
echo "sampled ${samples} times under load: lowest free=${min_free:--1},"\
" highest waiting=${max_wait}"

kill "${small}" "${large}" 2>/dev/null || true
wait "${small}" 2>/dev/null || true
wait "${large}" 2>/dev/null || true
wait "${flood}" 2>/dev/null || true
sleep 3

after="$(iobinfo)"
echo "iobinfo after the load: ${after}"

if diagnostics | grep -Eq "Assertion failed|PANIC|_assert:|Fatal"; then
  fail "the sim asserted under an exhausted pool"
fi
kill -0 "${nuttx_pid}" 2>/dev/null || fail "the sim died under an exhausted pool"
echo "PASS: no assertion and the sim survived"

recovered=false
for _ in $(seq 1 20); do
  sleep 2
  if ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1; then recovered=true; break; fi
done
[ "${recovered}" = true ] || fail "the tunnel did not recover after the flood"
echo "PASS: the tunnel carries traffic again"

final="$(iobinfo)"
echo "iobinfo at rest afterwards: ${final}"

python3 - "${start}" "${after}" "${final}" "${min_free:--1}" "${max_wait}" \
      "${samples}" <<'PY'
import sys

def parse(text):
    parts = text.split()
    return [int(p) for p in parts] if len(parts) == 4 else None

names = ("at rest", "after load", "at rest afterwards")
rows = [parse(a) for a in sys.argv[1:4]]
min_free = int(sys.argv[4])
max_wait = int(sys.argv[5])
samples = int(sys.argv[6])
failures = []

def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures.append(what)

for name, row in zip(names, rows):
    check(row is not None, "the %s sample parsed" % name)
if any(r is None for r in rows):
    sys.exit(1)

total = rows[0][0]
print("  pool of %d: free %s" % (total, " -> ".join(str(r[1]) for r in rows)))
print("  under load: %d samples, lowest free %d, highest waiting %d"
      % (samples, min_free, max_wait))

check(samples >= 8,
      "the pool was sampled enough times under load (%d)" % samples)

# Without this the run says nothing about exhaustion: a pool that stayed full
# was never short, and there is no cumulative counter to fall back on.
check(min_free == 0 or max_wait > 0,
      "the pool was genuinely driven down (lowest free %d, highest waiting %d)"
      % (min_free, max_wait))

check(rows[-1][1] == total,
      "the pool is whole again at rest (%d/%d)" % (rows[-1][1], total))
check(rows[-1][2] == 0, "nothing is left waiting at rest")

if failures:
    print("FAIL: %d check(s) failed" % len(failures))
    sys.exit(1)
print("PASS: the driver survives an exhausted IOB pool and recovers")
PY
