/**
 * lwip/udp.h compatibility shim for NuttX
 *
 * wireguard.h uses struct udp_pcb only as a pointer (struct udp_pcb *udp_pcb)
 * inside wireguard_device. The actual UDP handling in nuttx-wireguardif.c
 * uses BSD sockets (sys/socket.h) instead of lwIP UDP PCBs.
 */

#ifndef LWIP_UDP_H_NUTTX_COMPAT
#define LWIP_UDP_H_NUTTX_COMPAT

/* Opaque forward declaration. Replaced by BSD socket fd in nuttx-wireguardif.c */
struct udp_pcb;

#endif /* LWIP_UDP_H_NUTTX_COMPAT */
