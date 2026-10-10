#!/usr/bin/env bash
# Validate the local NuttX/apps submission series before publication.
#
# Usage:
#   verify-pr-series.sh [--require-signoff] [--tools <nuttx-tree>] SERIES...
#
# SERIES is <tree>:<base>..<tip>=<count>, one per PR, for example the layout
# of docs/upstream/merge-strategy.md section 2:
#
#   verify-pr-series.sh \
#     /opt/nuttx:upstream/master..pr/crypto-chachapoly=2 \
#     /opt/nuttx:upstream/master..pr/nxstyle-wg=1 \
#     /opt/nuttx:pr/crypto-chachapoly..pr/wireguard-driver=6 \
#     /opt/nuttx:pr/wireguard-driver..pr/doc-system-wg=1 \
#     /opt/apps:upstream/master..pr/system-wg=2
#
# For every series it checks the commit count and that there are no merge
# commits, then checks out each commit in turn (detached) and runs
# checkpatch.sh -c -u -g on that one commit, so every commit is checked on
# its own and not only the tip.  checkpatch runs nxstyle on the whole of
# each touched file as it is in that commit.
#
# Commit messages are always checked.  Without --require-signoff a
# placeholder Signed-off-by is appended for the check only (the commits are
# not modified), so everything except the sign-off itself is verified before
# the author signs.  With --require-signoff the real messages must pass
# checkpatch -m, sign-off included.
#
# A NuttX tree is checked with its own tools/checkpatch.sh: nxstyle bakes in
# the TOPDIR it was built in and rejects NuttX files from any other tree.
# An apps tree is checked with the checkpatch.sh of --tools (default
# /opt/nuttx).  That NuttX tree must contain tools/nxstyle.c from
# pr/nxstyle-wg, or the apps series fails on the vendored wg_x25519.[ch];
# use a separate checkout (for example a git worktree) when the NuttX tree
# is also being checked, since checked trees move commit by commit.
#
# Every tree must be clean.  Each tree's original HEAD is restored on exit.
set -euo pipefail

require_signoff=0
tools_tree=""
series=()

while [ "$#" -gt 0 ]; do
  case "$1" in
    --require-signoff) require_signoff=1 ;;
    --tools) tools_tree="$2"; shift ;;
    -h|--help) sed -n 2,37p "$0"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) series+=("$1") ;;
  esac
  shift
done

if [ "${#series[@]}" -eq 0 ]; then
  echo "usage: $0 [--require-signoff] [--tools <nuttx-tree>] <tree>:<base>..<tip>=<count>..." >&2
  exit 2
fi

declare -A orig_head=()
restore() {
  local t
  for t in "${!orig_head[@]}"; do
    git -C "$t" checkout -q "${orig_head[$t]}" 2>/dev/null || true
  done
}
trap restore EXIT

for s in "${series[@]}"; do
  tree="${s%%:*}"
  [ -d "$tree/.git" ] || [ -f "$tree/.git" ] ||
    { echo "FAIL: not a git tree: $tree" >&2; exit 1; }
  if [ -z "${orig_head[$tree]+x}" ]; then
    git -C "$tree" diff --quiet HEAD ||
      { echo "FAIL: dirty worktree: $tree" >&2; exit 1; }
    orig_head[$tree]=$(git -C "$tree" symbolic-ref -q --short HEAD ||
                       git -C "$tree" rev-parse HEAD)
  fi
done
tools_tree="${tools_tree:-/opt/nuttx}"
apps_checkpatch="$(cd "$tools_tree" && pwd)/tools/checkpatch.sh"

rc=0
for s in "${series[@]}"; do
  tree="${s%%:*}"
  rest="${s#*:}"
  range="${rest%%=*}"
  want="${rest##*=}"
  [ "$range" != "$rest" ] && [[ "$want" =~ ^[0-9]+$ ]] ||
    { echo "FAIL: bad series '$s' (want <tree>:<base>..<tip>=<count>)" >&2; exit 2; }

  if [ -f "$tree/tools/nxstyle.c" ]; then
    checkpatch="$(cd "$tree" && pwd)/tools/checkpatch.sh"
  else
    checkpatch="$apps_checkpatch"
    [ -x "$checkpatch" ] ||
      { echo "FAIL: no checkpatch.sh in $tools_tree" >&2; exit 1; }
  fi

  echo "=== $tree $range"
  count=$(git -C "$tree" rev-list --count "$range")
  merges=$(git -C "$tree" rev-list --count --merges "$range")
  if [ "$count" -ne "$want" ]; then
    echo "FAIL: expected $want commits, found $count"
    rc=1
  fi
  if [ "$merges" -ne 0 ]; then
    echo "FAIL: $merges merge commit(s) in $range"
    rc=1
  fi

  for c in $(git -C "$tree" rev-list --reverse "$range"); do
    echo "--- $(git -C "$tree" log -1 --format='%h %s' "$c")"
    git -C "$tree" checkout -q --detach "$c"
    if ! (cd "$tree" && "$checkpatch" -c -u -g HEAD~1..HEAD); then
      echo "FAIL: patch check: $c"
      rc=1
    fi
    if [ "$require_signoff" -eq 1 ]; then
      (cd "$tree" && "$checkpatch" -m -g HEAD~1..HEAD) ||
        { echo "FAIL: message check: $c"; rc=1; }
    else
      printf '%s\n\nSigned-off-by: Placeholder <placeholder@example.invalid>\n' \
        "$(git -C "$tree" show -s --format=%B "$c")" |
        (cd "$tree" && "$checkpatch" -m -g --stdin) ||
        { echo "FAIL: message check: $c"; rc=1; }
      if git -C "$tree" show -s --format=%B "$c" | grep -q '^Signed-off-by:'; then
        echo "note: already signed"
      fi
    fi
  done
done

[ "$rc" -eq 0 ] ||
  { echo "FAIL: one or more PR-series checks failed" >&2; exit "$rc"; }

if [ "$require_signoff" -eq 0 ]; then
  echo "PASS: series shape, per-commit patch checks and messages (sign-off not required)"
else
  echo "PASS: series shape, per-commit patch checks, messages and sign-offs"
fi
