#!/usr/bin/env bash
# Sequential regressions: these tests share tap0, wgtest0 and /tmp NSH FIFOs.
# Run only in a dedicated container/network namespace, after the sim build.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
logs=$(mktemp -d /tmp/wg-sim-suite.XXXXXX)
if ! grep -qx 'CONFIG_SYSTEM_WG_CONFIG_PATH="/tmp/wg0.conf"' /opt/nuttx/.config; then
  echo 'FAIL: this sim suite requires CONFIG_SYSTEM_WG_CONFIG_PATH="/tmp/wg0.conf"'
  exit 1
fi
echo "Logs: ${logs} (may contain ephemeral test keys; do not publish raw)"
nuttx_head=$(git -C /opt/nuttx rev-parse HEAD)
apps_head=$(git -C /opt/apps rev-parse HEAD)
echo "${nuttx_head}"
echo "${apps_head}"
sha256sum /opt/nuttx/.config

check_revisions() {
  if [ "$(git -C /opt/nuttx rev-parse HEAD)" != "${nuttx_head}" ] ||
     [ "$(git -C /opt/apps rev-parse HEAD)" != "${apps_head}" ]; then
    echo 'FAIL: source revision changed during the suite'
    exit 1
  fi
  git -C /opt/nuttx diff --quiet HEAD
  git -C /opt/apps diff --quiet HEAD
}

tests=(
  verify-sim-wg-runtime.sh
  verify-sim-wg-ioctl.sh
  verify-sim-wg-kat.sh
  verify-sim-wg-negotiation.sh
  verify-sim-wg-multipeer.sh
  verify-sim-wg-replay.sh
  verify-sim-wg-zeroize.sh
  verify-sim-wg-keyfile-faults.sh
)
for test in "${tests[@]}"; do
  check_revisions
  start=$SECONDS
  echo "RUN: ${test}"
  if PYTHONUNBUFFERED=1 bash "${script_dir}/${test}" >"${logs}/${test}.log" 2>&1; then
    echo "PASS: ${test} ($((SECONDS - start))s)"
  else
    rc=$?
    echo "FAIL: ${test} (exit ${rc})"
    sed -E 's/[A-Za-z0-9+\/]{43}=/[redacted-key]/g' "${logs}/${test}.log" | tail -60
    exit "${rc}"
  fi
done
check_revisions
python3 "${script_dir}/test-wg-file-publish.py"
echo "PASS: complete sim suite"
