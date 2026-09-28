#!/usr/bin/env bash
# T7: soak. Rekeys past the expiration horizon, endpoint changes, and a long
# run of down/up cycles, while watching the three resources that a leak in
# this driver would show up in:
#
#   heap        /proc/meminfo is not enough; "free" also gives the live
#               allocation count, which catches a leak that fragmentation
#               would otherwise hide
#   IOB         the driver holds a netpkt_queue_t, so a queue entry or buffer
#               that is not returned shows as iobinfo nfree < ntotal at rest
#   stack       ps reports the high-water mark per task (needs
#               CONFIG_STACK_COLORATION), including the wg_rx thread
#
# Usage: verify-sim-wg-soak.sh [rekey_seconds] [downup_cycles]
#
# The rekey phase must outlast REJECT_AFTER_TIME x 3 (540 s) so sessions are
# actually expired and rebuilt rather than just refreshed, which is why the
# default is 620 s and the whole run takes roughly 15 minutes.
#
# A rekey is forced by dropping and re-adding the peer on the Linux side: it
# then has to initiate, and NuttX has to respond and install a new key pair.
# This is the same lever the zeroization test uses.
#
# Pass = every phase completes, traffic works at the end of each, and at rest
# the IOB pool is whole, the heap is back where it started, and no stack
# high-water mark grew during the later phases.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd /opt/nuttx
REKEY_SECONDS="${1:-620}"
DOWNUP_CYCLES="${2:-100}"
REKEY_INTERVAL=4
REKEYS_WANTED=50
SMOKE=false

# Shorter than the expiration horizon is a shakedown of this script, not T7.
# Say so loudly rather than letting a weaker run print the same PASS.
if [ "${REKEY_SECONDS}" -lt 540 ] || [ "${DOWNUP_CYCLES}" -lt 100 ]; then
  SMOKE=true
  REKEYS_WANTED=3
  echo "NOTE: shakedown run (${REKEY_SECONDS}s rekeys, ${DOWNUP_CYCLES} "\
"down/up cycles). This does NOT satisfy T7, which needs more than "\
"REJECT_AFTER_TIME x 3 = 540s and 100 cycles."
fi

grep -q "^CONFIG_STACK_COLORATION=y" .config ||
  { echo "FAIL: needs CONFIG_STACK_COLORATION=y for stack high-water marks";
    exit 1; }

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

linux_priv="$(wg genkey)"; linux_pub="$(printf %s "${linux_priv}" | wg pubkey)"
printf %s "${linux_priv}" > /tmp/linux.key

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  set +e
  ip link del wgtest0 2>/dev/null
  printf "poweroff\n" >&3 2>/dev/null; sleep 1
  kill "${nuttx_pid}" 2>/dev/null; wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}
trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2
send() { printf "%s\n" "$1" >&3; sleep "${2:-0.5}"; }
fail() { echo "FAIL: $1"; tail -60 /tmp/nuttx.out; exit 1; }
alive() { kill -0 "${nuttx_pid}" 2>/dev/null || fail "the sim died"; }

# One resource sample, bracketed so the analyzer can find it in the log.
sample() {
  alive
  send "echo SOAKMARK $1"
  send "free"
  send "cat /proc/iobinfo"
  send "ps" 1.0
}

pingable() {
  for _ in $(seq 1 "${2:-6}"); do
    if ping -c 1 -W 2 10.10.0.2 >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  return 1
}

# Drop and re-add the peer so the Linux side must initiate a new handshake.
relinux() {
  wg set wgtest0 peer "${NPUB}" remove
  wg set wgtest0 peer "${NPUB}" allowed-ips 10.10.0.2/32 \
    endpoint "10.0.0.2:51820" persistent-keepalive 5
}

ip link show tap0 >/dev/null 2>&1 || fail "tap0 not created"
ip addr add 10.0.0.1/24 dev tap0 2>/dev/null || true
ip link set tap0 up

sample "boot"

send "wg genkey"
npriv="$(sed 's/\x1b\[K//g' /tmp/nuttx.out | grep -Eo '^[A-Za-z0-9+/]{43}=' | tail -1)"
[ -n "${npriv}" ] || fail "no key from genkey"
NPUB="$(printf %s "${npriv}" | wg pubkey)"
send "wg set private-key ${npriv}"
send "wg set address 10.10.0.2/24"
send "wg set peer ${linux_pub} endpoint 10.0.0.1:51821 allowed-ips 10.10.0.1/32 persistent-keepalive 5"
send "wg up"

ip link add wgtest0 type wireguard
wg set wgtest0 private-key /tmp/linux.key listen-port 51821
relinux
ip addr add 10.10.0.1/24 dev wgtest0; ip link set wgtest0 up
pingable || fail "the tunnel never came up"
sample "up"

# ---- phase A: rekeys past the expiration horizon -------------------------

echo "phase A: forcing rekeys for ${REKEY_SECONDS}s "\
"(REJECT_AFTER_TIME x 3 = 540s)"
rekeys=0; rekey_failed=0
phase_a_end=$(( $(date +%s) + REKEY_SECONDS ))
next_sample=$(( $(date +%s) + 120 ))
while [ "$(date +%s)" -lt "${phase_a_end}" ]; do
  relinux
  if pingable 8; then rekeys=$((rekeys + 1));
  else rekey_failed=$((rekey_failed + 1)); fi
  if [ "$(date +%s)" -ge "${next_sample}" ]; then
    sample "rekey-${rekeys}"
    next_sample=$(( $(date +%s) + 120 ))
  fi
  sleep "${REKEY_INTERVAL}"
done
echo "  rekeys completed: ${rekeys}, failed: ${rekey_failed}"
[ "${rekeys}" -ge "${REKEYS_WANTED}" ] ||
  fail "only ${rekeys} rekeys completed, wanted ${REKEYS_WANTED}+"
[ "${rekey_failed}" -eq 0 ] || fail "${rekey_failed} rekeys did not restore traffic"
sample "after-rekeys"

# ---- phase B: endpoint changes ------------------------------------------
# Both sides move to a new port and NuttX is cycled, so it has to dial out to
# the newly configured endpoint rather than reply to whatever last arrived.

echo "phase B: 10 endpoint changes"
endpoint_changes=0
for port in $(seq 51822 51831); do
  wg set wgtest0 listen-port "${port}"
  send "wg set peer ${linux_pub} endpoint 10.0.0.1:${port}"
  send "wg down" 1.0
  send "wg up" 1.0
  relinux
  pingable 10 || fail "traffic did not resume after moving to port ${port}"
  endpoint_changes=$((endpoint_changes + 1))
done
echo "  endpoint changes: ${endpoint_changes}"
[ "${endpoint_changes}" -eq 10 ] || fail "wanted 10 endpoint changes"
sample "after-endpoints"

# ---- phase C: down/up cycles --------------------------------------------

echo "phase C: ${DOWNUP_CYCLES} down/up cycles"
for cycle in $(seq 1 "${DOWNUP_CYCLES}"); do
  send "wg down" 0.6
  send "wg up" 0.6
  alive
  if [ $((cycle % 10)) -eq 0 ]; then
    relinux
    pingable 10 || fail "traffic did not resume after down/up cycle ${cycle}"
  fi
  if [ $((cycle % 25)) -eq 0 ]; then sample "downup-${cycle}"; fi
done
relinux
pingable 10 || fail "traffic did not resume after the last cycle"
sample "after-downup"

# ---- at rest: nothing should still be held ------------------------------

send "wg down" 2.0
sample "rest"

cat > /tmp/soak_analyze.py <<'PY'
import re, sys

log = open(sys.argv[1], "rb").read().decode("utf-8", "replace")
log = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", log).replace("\r", "")
lines = log.split("\n")

# Sample boundaries: the echoed output line, not the command NSH echoed back.
marks = [(i, m.group(1)) for i, line in enumerate(lines)
         for m in [re.match(r"^(?:nsh> )?SOAKMARK (\S+)\s*$", line)]
         if m and "echo SOAKMARK" not in line]

HEAP = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+Umem")
IOB = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$")
TASK = re.compile(r"([0-9a-f]{16})\s+(\d+)\s+(\d+)\s+([\d.]+)%\s+(\S+)")

samples = []
for index, (start, label) in enumerate(marks):
    end = marks[index + 1][0] if index + 1 < len(marks) else len(lines)
    block = lines[start:end]
    entry = {"label": label, "stacks": {}}
    for line in block:
        heap = HEAP.match(line)
        if heap and "heap_used" not in entry:
            entry["heap_used"] = int(heap.group(2))
            entry["heap_nused"] = int(heap.group(6))
        iob = IOB.match(line)
        if iob and "iob_free" not in entry:
            # The iobinfo row is the only bare 4-column integer line here.
            entry["iob_total"] = int(iob.group(1))
            entry["iob_free"] = int(iob.group(2))
            entry["iob_wait"] = int(iob.group(3))
        task = TASK.search(line)
        if task:
            entry["stacks"][task.group(5)] = (int(task.group(3)),
                                              float(task.group(4)))
    samples.append(entry)

if len(samples) < 4:
    print("FAIL: only %d samples parsed out of the log" % len(samples))
    sys.exit(1)

print()
print("%-16s %10s %7s %9s %10s %10s" %
      ("sample", "heap used", "allocs", "iob free", "wg_rx used", "sh used"))
for s in samples:
    wg_rx = s["stacks"].get("wg_rx", ("-", 0))[0]
    sh = s["stacks"].get("sh", ("-", 0))[0]
    print("%-16s %10s %7s %9s %10s %10s" %
          (s["label"], s.get("heap_used", "?"), s.get("heap_nused", "?"),
           "%s/%s" % (s.get("iob_free", "?"), s.get("iob_total", "?")),
           wg_rx, sh))

failures = []
def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures.append(what)

boot = samples[0]
rest = samples[-1]

check(rest["iob_free"] == rest["iob_total"],
      "the IOB pool is whole at rest (%d/%d)"
      % (rest["iob_free"], rest["iob_total"]))
check(all(s.get("iob_wait", 0) == 0 for s in samples),
      "no task ever waited for an IOB")

# The allocation count is the leak detector: a buffer, peer snapshot or queue
# entry that is never freed raises it and never lowers it again.
allocs_slack = 4
check(rest["heap_nused"] <= boot["heap_nused"] + allocs_slack,
      "live allocations return to the boot count (%d -> %d, slack %d)"
      % (boot["heap_nused"], rest["heap_nused"], allocs_slack))

heap_slack = 8192
check(rest["heap_used"] <= boot["heap_used"] + heap_slack,
      "heap in use returns to the boot level (%d -> %d, slack %d B)"
      % (boot["heap_used"], rest["heap_used"], heap_slack))

# A high-water mark can only rise, so the question is whether it settles.
# Anything the driver does in the later phases it also did in the first, so a
# late rise means a path whose depth depends on how long it has been running.
rx_marks = [(s["label"], s["stacks"]["wg_rx"])
            for s in samples if "wg_rx" in s["stacks"]]
if len(rx_marks) >= 3:
    early = max(u for _, (u, _) in rx_marks[:2])
    late = max(u for _, (u, _) in rx_marks[2:])
    check(late <= early,
          "the wg_rx stack high-water does not grow after the first samples "
          "(%d -> %d)" % (early, late))
    worst = max(f for _, (_, f) in rx_marks)
    check(worst < 70.0,
          "the wg_rx stack stays clear of its limit (worst %.1f%% filled)"
          % worst)
else:
    check(False, "the wg_rx thread was sampled at least three times")

if failures:
    print("FAIL: %d check(s) failed" % len(failures))
    sys.exit(1)
print("PASS: no heap, IOB or stack growth across the soak")
PY

python3 /tmp/soak_analyze.py /tmp/nuttx.out
echo "rekeys=${rekeys} endpoint_changes=${endpoint_changes} "\
"downup_cycles=${DOWNUP_CYCLES}"
if [ "${SMOKE}" = true ]; then
  echo "NOTE: shakedown parameters, so this is not a T7 result."
else
  echo "PASS: T7 soak"
fi
