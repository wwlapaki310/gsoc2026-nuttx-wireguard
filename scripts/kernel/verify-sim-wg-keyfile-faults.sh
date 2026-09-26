#!/usr/bin/env bash
# Acceptance test for the configuration-save correctness fix (#17).
#
# The driver never returns the private key, so CONFIG_SYSTEM_WG_CONFIG_PATH is
# the only copy. Before the fix, a failed save was reported as success and the
# previous file was unlink()ed before the replacement, so a failure destroyed
# it. This injects a save failure and checks the three properties that matter:
#
#   1. a failed save exits nonzero and says the running key is not stored
#   2. the previous configuration is still there afterwards
#   3. the divergence is observable: the device holds the new key while the
#      file still holds the old one, and setconf brings the old one back
#
# The fault is injected by making the temporary path a directory, so the
# fopen() of "<config>.tmp" cannot succeed. No peer or network is needed.
#
# Pass = every checked command has the expected status and the interface
# public key follows the file, not the failed write.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd /opt/nuttx

conf=$(sed -n 's/^CONFIG_SYSTEM_WG_CONFIG_PATH="\(.*\)"$/\1/p' .config)
if [ -z "${conf}" ]; then
  echo "SKIP: CONFIG_SYSTEM_WG_CONFIG_PATH not set"
  exit 77
fi

if ! command -v wg >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y -qq wireguard-tools >/tmp/apt-wireguard.log
fi

# Two known keys, so the public key in "wg show" says which one is in effect.
K1="$(wg genkey)"; P1="$(printf %s "${K1}" | wg pubkey)"
K2="$(wg genkey)"; P2="$(printf %s "${K2}" | wg pubkey)"

rm -f /tmp/nuttx.in /tmp/nuttx.out
mkfifo /tmp/nuttx.in
/opt/nuttx/nuttx </tmp/nuttx.in >/tmp/nuttx.out 2>&1 &
nuttx_pid=$!

cleanup() {
  set +e
  printf "poweroff\n" >&3 2>/dev/null; sleep 1
  kill "${nuttx_pid}" 2>/dev/null; wait "${nuttx_pid}" 2>/dev/null
  rm -f /tmp/nuttx.in
}
trap cleanup EXIT

exec 3>/tmp/nuttx.in
sleep 2
send() { printf "%s\n" "$1" >&3; sleep 0.6; }
fail() { echo "FAIL: $1"; diagnostics | tail -60; exit 1; }
diagnostics() {
  sed 's/\x1b\[[0-9;]*[[:alpha:]]//g; s/\r//g; s/^nsh> //' /tmp/nuttx.out
}

tags=()
checked() {
  local tag="WGCHK_$1" command="$2" expected="${3:-0}" rc
  tags+=("${tag}=${expected}")
  send "${command}"
  send "echo ${tag}:\$?"
  for _ in $(seq 1 60); do
    rc=0
    python3 "${script_dir}/nsh-status.py" /tmp/nuttx.out "${tags[@]}" || rc=$?
    [ "${rc}" -eq 0 ] && return 0
    [ "${rc}" -eq 1 ] && fail "unexpected status for: ${command}"
    sleep 0.2
  done
  fail "no completed result for: ${command}"
}

# The interface public key as currently held by the device.
shown_pubkey() {
  send "wg show"
  sleep 0.4
  diagnostics | sed -n 's/^ *public key: \([A-Za-z0-9+/]\{43\}=\)$/\1/p' | tail -1
}

# --- baseline: a good save -------------------------------------------------

checked SETK1 "wg set private-key ${K1}"
[ "$(shown_pubkey)" = "${P1}" ] || fail "device did not take the first key"
echo "PASS: baseline key stored"

# --- fault: the temporary file cannot be created ---------------------------

send "mkdir ${conf}.tmp"
checked SETK2 "wg set private-key ${K2}" 1

if ! diagnostics | grep -q "could not be saved"; then
  fail "a failed save did not report that the running key is unsaved"
fi
echo "PASS: failed save exits nonzero and says the key is not stored"

# The device took the key even though the file did not: that divergence is
# exactly what the warning is about, so assert it rather than hide it.
[ "$(shown_pubkey)" = "${P2}" ] || fail "device should hold the new key"

send "rmdir ${conf}.tmp"

# --- the previous configuration must have survived -------------------------

checked RELOAD1 "wg setconf ${conf}"
[ "$(shown_pubkey)" = "${P1}" ] ||
  fail "the previous configuration was lost by the failed save"
echo "PASS: previous configuration survived the failed save"

# --- the same fault against saveconf ---------------------------------------

send "mkdir ${conf}.tmp"
checked SAVEFAIL "wg saveconf" 1
send "rmdir ${conf}.tmp"

checked RELOAD2 "wg setconf ${conf}"
[ "$(shown_pubkey)" = "${P1}" ] ||
  fail "a failed saveconf damaged the configuration"
echo "PASS: failed saveconf exits nonzero and leaves the file intact"

# --- recovery once the fault is removed ------------------------------------

checked SETK2OK "wg set private-key ${K2}"
checked RELOAD3 "wg setconf ${conf}"
[ "$(shown_pubkey)" = "${P2}" ] || fail "save did not recover after the fault"
echo "PASS: saving works again once the fault is removed"

echo "PASS: sim WireGuard key/config save fault handling verified (#17)"
