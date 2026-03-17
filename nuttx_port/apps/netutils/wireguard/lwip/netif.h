/**
 * lwip/netif.h compatibility shim for NuttX
 *
 * wireguard.h uses struct netif only as a pointer (struct netif *netif)
 * inside wireguard_device. The actual definition lives in nuttx-wireguardif.c.
 */

#ifndef LWIP_NETIF_H_NUTTX_COMPAT
#define LWIP_NETIF_H_NUTTX_COMPAT

#include "ip_addr.h"
#include "arch.h"

/* Opaque forward declaration. Defined in nuttx-wireguardif.c */
struct netif;

#endif /* LWIP_NETIF_H_NUTTX_COMPAT */
