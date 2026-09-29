#!/usr/bin/env bash
# Validate the local NuttX/apps submission series before publication.
set -euo pipefail

nuttx_tree="${NUTTX_TREE:-/opt/nuttx}"
apps_tree="${APPS_TREE:-/opt/apps}"
nuttx_base="${NUTTX_BASE:-68dd87f4df9ad1e19240136b931867efbcbc23d0}"
apps_base="${APPS_BASE:-b66303e26aa537dd74d6abaeeeded81c151a7e35}"
message_args=()

if [ "${1:-}" = "--require-signoff" ]; then
  message_args=(-m)
elif [ "$#" -ne 0 ]; then
  echo "usage: $0 [--require-signoff]" >&2
  exit 2
fi

for tree in "$nuttx_tree" "$apps_tree"; do
  git -C "$tree" diff --quiet HEAD ||
    { echo "FAIL: dirty worktree: $tree" >&2; exit 1; }
done

nuttx_count=$(git -C "$nuttx_tree" rev-list --count "$nuttx_base..HEAD")
apps_count=$(git -C "$apps_tree" rev-list --count "$apps_base..HEAD")
[ "$nuttx_count" -eq 2 ] ||
  { echo "FAIL: expected 2 NuttX commits, found $nuttx_count" >&2; exit 1; }
[ "$apps_count" -eq 1 ] ||
  { echo "FAIL: expected 1 apps commit, found $apps_count" >&2; exit 1; }

echo "NuttX: $nuttx_base..$(git -C "$nuttx_tree" rev-parse HEAD)"
rc=0
(cd "$nuttx_tree" &&
  ./tools/checkpatch.sh "${message_args[@]}" -g "$nuttx_base..HEAD") || rc=1

echo "apps:  $apps_base..$(git -C "$apps_tree" rev-parse HEAD)"
(cd "$apps_tree" &&
  "$nuttx_tree/tools/checkpatch.sh" "${message_args[@]}" -g \
    "$apps_base..HEAD") || rc=1

[ "$rc" -eq 0 ] ||
  { echo "FAIL: one or more PR-series checks failed" >&2; exit "$rc"; }

if [ "${#message_args[@]}" -eq 0 ]; then
  echo "PASS: PR series shape and patch checks"
else
  echo "PASS: PR series shape, patch checks, and commit sign-offs"
fi
