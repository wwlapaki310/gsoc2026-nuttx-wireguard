# Rebased submission candidate

These signed review branches are published in the owner's forks, but are not
yet Apache upstream PRs.

| Repository | Upstream base | Candidate tip |
| --- | --- | --- |
| apache/nuttx | `68dd87f4df9ad1e19240136b931867efbcbc23d0` | `c0ead14d8337bde212f5f01da0e4a2d089870170` |
| apache/nuttx-apps | `b66303e26aa537dd74d6abaeeeded81c151a7e35` | `e4f910dc18523a4c7edf586924723683b536196b` |

The `nuttx/` series contains crypto `a2dd121201` (nonce fix **and** both AEAD
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

The owner reviewed and authorized DCO certification; all three patches now carry
`Signed-off-by: satoru akita <wwlap24@gmail.com>`. The publication gate passes
with `-m`. Fork branches: [crypto](https://github.com/wwlapaki310/nuttx/tree/wireguard-crypto),
[driver](https://github.com/wwlapaki310/nuttx/tree/wireguard-driver), and
[apps command](https://github.com/wwlapaki310/nuttx-apps/tree/wireguard-wg).
See the integration validation record for tested configurations and limits.
