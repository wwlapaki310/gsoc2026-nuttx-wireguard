# Rebased submission candidate

These are local review branches, not published upstream PRs.

| Repository | Upstream base | Candidate tip |
| --- | --- | --- |
| apache/nuttx | `68dd87f4df9ad1e19240136b931867efbcbc23d0` | `8defcefa947645d2245a320dc2d0e0c2eb8ce6cd` |
| apache/nuttx-apps | `b66303e26aa537dd74d6abaeeeded81c151a7e35` | `c039b232e5fcf181ac4d16b9231cdc42a025d45c` |

The `nuttx/` series contains crypto `a5d9148cf5` (nonce fix **and** both AEAD
KAT families), followed by the driver. The `apps/` patch contains the command
and recoverable configuration publisher. Apply each directory in numeric order
with `git am` on the matching repository/base. Do not apply the older standalone
recovery patch on top: it is already included here.

Local worktrees are `../nuttx-integration-20260928` and
`../nuttx-apps-integration-20260928`, both on `review/pr-series-20260928`.
`review/upstream-20260928` retains the unsquashed rebase for comparison.
Original `net-wireguard` and `system-wg` branches were not rewritten or pushed.

Beyond the rebase, the candidate fixes style in files already touched by the
series. The vendored X25519 algorithm is unchanged; two header-comment spelling
errors were corrected, without reformatting it. The raw rebased tree and this
submission tree therefore are not byte-identical.

**Author action is still required:** review the commits and add your own
`Signed-off-by` certification if appropriate before publishing. No signature
was invented. The CI command includes `-m` and will reject unsigned commits;
a passing code/style check without `-m` must not be reported as full CI passing.
See the integration validation record for tested configurations and limits.
