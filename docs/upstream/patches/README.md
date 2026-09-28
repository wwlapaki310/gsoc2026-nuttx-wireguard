# Local follow-up patches

These patches publish reviewable changes in the coordination repository without
pushing the NuttX/apps forks or opening upstream PRs. The repository owner still
prepares and publishes the upstream series, per `../handoff.md`.

The configuration recovery patch applies to the apps `system-wg` branch at
`691311a4`. It is not the complete apps WireGuard implementation or a standalone
upstream PR. Apply with `git am` only on that base (or after resolving a rebase).
See `../keyfile-correctness-review.md` for behavior and recovery instructions.
