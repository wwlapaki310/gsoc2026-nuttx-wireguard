/**
 * lwip/ip_addr.h compatibility shim for NuttX
 *
 * Maps lwIP IP address types to POSIX equivalents.
 * wireguard.c stores and copies ip_addr_t values but does not access
 * lwIP-specific fields (those are only in wireguardif.c which we replace).
 */

#ifndef LWIP_IP_ADDR_H_NUTTX_COMPAT
#define LWIP_IP_ADDR_H_NUTTX_COMPAT

#include <netinet/in.h>

typedef struct in_addr ip4_addr_t;
typedef struct in_addr ip_addr_t;

#endif /* LWIP_IP_ADDR_H_NUTTX_COMPAT */
