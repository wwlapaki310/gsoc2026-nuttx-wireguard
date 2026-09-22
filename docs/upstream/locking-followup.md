# Device-lock and queued-output follow-up

Date: 2026-09-22. Work in progress on fork `66b7403c8a`; not committed or pushed.
This note is the handoff for the concurrency review, not upstream acceptance evidence.

## Changes

- Protocol state uses the upper half's device `d_lock`, not global `net_lock`.
- `wg_tx_enqueue` copies ciphertext and endpoint into an immutable, bounded queue.
  Four entries maximum includes the socket worker's in-flight entry.
- Only the RX worker calls `psock_sendto`, outside `d_lock`, with no live peer/keypair
  references. Queue admission, not backend completion, updates peer TX accounting.
- RX notification uses direct upper-half mode outside protocol processing, avoiding
  a synchronous work-cancel wait against a worker blocked on the caller's device lock.
- Upperhalf VLAN fan-out skips non-Ethernet parents before taking the global lock,
  matching `vlan_register`'s Ethernet-only restriction. This also covers TX-done calls
  on the TUN while its device lock is held.
- Stop wakes the worker, temporarily releases the device lock, and excludes a second
  reap waiter. Timeout preserves resources; repeated down reaps them. Key replacement,
  peer deletion, and WireGuard up are refused until the stopping thread is reaped.
- Queue pressure drops packets without requesting an unnecessary new handshake.
- NSH tests parse complete result lines and target-side exit codes in order. They reject
  input echoes, failed commands, duplicates, reordered results, and partial records.
- `sim:wireguard` now specifies command-line capacity and a writable configuration path;
  the old defconfig truncated peer commands and could not persist `/data/wg0.conf`.

## Independent Environment

The existing `wgdev` container was not modified. A separate `wg-codex-locking` container
was created from `nuttx-wireguard:sim-master` with NET_ADMIN and `/dev/net/tun`. NuttX and
apps were populated from host `git archive HEAD`, then the reviewed working-tree files
were copied in. The old image source trees were moved aside, not overlaid.
Apps baseline: `24b3f31157e54082206435a9af6674efcc090753`.

The tested container and host working-tree files have matching SHA-256 hashes:

```text
wireguard.c       cde1a2c457c72bfb4cd99b37ef8ec520a7f2ccf8d814d25c7080b4c76a41f5c7
netdev_upperhalf.c fe0d5ca304984a1c9177d67e0c3a33a207b1ef66f9e7d79f186de602c24d2c8a
```

## Validation

Confirmed independently in the clean container:

- Host status-parser tests: 10 PASS, including expected failure and unterminated records.
- T1 runtime, TF ioctl rejection, TR replay/cookie, TN negative interoperability,
  T3 simultaneous peers: PASS on the final code with `CONFIG_NET_VLAN=y`.
- 25 lifecycle cycles: PASS on the final VLAN-enabled code; flood active for down/up,
  paused for recovery probes. An earlier non-VLAN 25-cycle run also passed.
- Output-stall injection: PASS; control query/update progressed, the queue reached four
  owned entries, retained ciphertext passed its integrity assertion, down returned
  ETIMEDOUT, key replacement/up were rejected, and repeated down/up restored traffic.
- Stop-stall injection: PASS, including a concurrent second down returning EBUSY rather
  than competing for the exit semaphore.
- WireGuard file style check: PASS. The modified upperhalf range passes `nxstyle`;
  a full-file upperhalf check reports existing style violations outside the changed range.

Build logs, final `.config`, and test logs are retained locally in
`output/codex-locking-validation/`. The dedicated container is retained stopped for
inspection (`docker start wg-codex-locking`); the original `wgdev` was untouched.

### Reproduce

In a clean Linux build tree containing the fork changes and sibling apps checkout:

```sh
./tools/configure.sh sim:wireguard
make -j8
bash /path/to/scripts/kernel/verify-sim-wg-runtime.sh
bash /path/to/scripts/kernel/verify-sim-wg-downup.sh 25
```

The runtime scripts expect this tree at `/opt/nuttx`, a Linux host with WireGuard,
NET_ADMIN, `/dev/net/tun`, and `wg`/`ip`/`ping` installed. Keep `nsh-status.py` beside the
lifecycle scripts. To reproduce the VLAN-enabled variant, enable `CONFIG_NET_VLAN`
with `kconfig-tweak`, run `make olddefconfig`, and rebuild.

For the fault tests, enable exactly one of `CONFIG_NET_WIREGUARD_DEBUG_STOP_STALL`
and `CONFIG_NET_WIREGUARD_DEBUG_TX_STALL`, run `make olddefconfig` and rebuild, then run
`verify-sim-wg-stop-recovery.sh stop` or `verify-sim-wg-stop-recovery.sh output`.
Output mode also requires `CONFIG_SYSTEM_PING=y` (included in the updated defconfig).
Restore both debug options to disabled for normal regression tests. Missing prerequisites
produce exit 77, not a successful test.

### Failed probes and corrections

Clean builds exposed configuration gaps hidden by the previous container's local settings:
short NSH input lines, an unavailable `/data` configuration path, a one-peer limit, and
the ping library without the ping command. These are now explicit in `sim:wireguard`.
The asynchronous-down diagnostic can appear after an NSH prompt on the same line; the
recovery test strips that prompt for errno checks, not for completion-status parsing.

An initial lifecycle probe left the flood active during every recovery ping. Four cycles
passed, then a recovery probe timed out. Its cause was not isolated; it is not evidence
that the failure was merely a simulator flake. The test now states the narrower lifecycle
property explicitly and pauses the flood for recovery. Sustained-DoS availability remains
an open observation, not a solved bug or a claimed PASS.

## Remaining Limits

- A daemon/backend that never finishes can still stop this worker's RX and timer progress.
  The redesign prevents shared-buffer corruption and premature close, not that outage.
- The output-stall injection exercises ownership, control-plane progress, queue pressure,
  and shutdown. It does not emulate usrsock protocol messages, disconnects, or its mutexes.
- No fairness guarantee across peers; the four-entry FIFO trades memory for burst capacity.
  Additional live output storage is bounded by four times the outbound-entry header plus
  `WG_CRYPT_BUFSIZE` (about 6 KiB at the default MTU), with allocation per datagram.
- SMP, PROTECTED/KERNEL reruns, real usrsock disconnects, and board memory/latency measurements
  have not been performed for this revision. Direct RX processing also needs board stack
  high-water measurements before claiming the existing stack default is sufficient there.
- TAI64N reboot/rollback remains unresolved in issue #14. No claim of merge readiness.
- Do not conflate paused-flood recovery probes with availability during sustained flooding.
