# WireGuard IOB allocation audit (2026-09-28)

Scope: NuttX `b230ee4876`, queued-output driver, buffered UDP configuration.
This is a static reachability audit, not a new stress-test result.

| Path | Allocation behavior |
| --- | --- |
| `wg_flush_tx` | Sends with `MSG_DONTWAIT` outside `d_lock`. |
| `udp_sendto_buffered.c:psock_udp_sendto` | Derives `nonblock` from that flag; send-buffer limit returns `-EAGAIN`; calls `udp_wrbuffer_tryalloc`, then `iob_trycopyin`. |
| `udp_wrbuffer.c:udp_wrbuffer_tryalloc` | Uses `iob_tryalloc(false)` for ordinary frames. |
| WG receive injection | Calls `netpkt_alloc`, `netpkt_copyin`, `netpkt_tryadd_queue`. |
| `netdev_upperhalf.c:netpkt_alloc` | The non-CAN path uses `iob_tryalloc(false)` and restores quota on failure. |
| `netdev_upperhalf.c:netpkt_copyin` | Uses `iob_trycopyin`, not the waiting copy operation. |

For these direct paths, exhausting IOBs should cause failure/drop and recovery,
not a waiting allocator. TI's observed `nwait=0` therefore does not need a new
test that changes the driver to block just to make the counter nonzero.

This does **not** prove that all of `psock_sendto` is nonblocking: address
resolution, unbuffered UDP, and usrsock have different call chains. In particular,
the usrsock send path can discard `MSG_DONTWAIT`. The queued datagram's ownership
and stopping/reaping protocol still matter. The existing DEBUG_TX_STALL test
holds a datagram but does not reproduce a daemon/backend blocked inside send.
Real backend stalls and AP loss remain separate follow-ups. Jumbo/dynamic IOB
configurations and unrelated stack allocation paths are outside this audit.
