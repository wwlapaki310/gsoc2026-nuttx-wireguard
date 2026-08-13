/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.h
 *
 * Public entry point for the NuttX WireGuard network interface.
 ****************************************************************************/

#ifndef __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H
#define __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H

/****************************************************************************
 * Name: wg_initialize
 *
 * Description:
 *   Register the "wg0" network interface, configure it (private key,
 *   listen port, local address, and optionally one peer) from Kconfig
 *   settings, and start the background thread that handles the UDP
 *   transport for the tunnel.
 *
 *   Safe to call more than once; subsequent calls are a no-op once the
 *   interface is up.
 *
 * Returned Value:
 *   0 (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int wg_initialize(void);

/****************************************************************************
 * Name: wg_show
 *
 * Description:
 *   Print wg0's interface and peer status to stdout (public key, listening
 *   port, and per-peer endpoint / latest handshake / transfer counters).
 *   Backs the "wg show" NSH subcommand.
 *
 ****************************************************************************/

void wg_show(void);

#endif /* __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H */
