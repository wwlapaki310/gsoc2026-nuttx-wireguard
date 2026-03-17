/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.c
 *
 * NuttX network interface layer for WireGuard.
 *
 * This file replaces wireguardif.c (which requires upstream lwIP APIs not
 * available in NuttX). It registers WireGuard as a virtual network device
 * using NuttX's netdev API.
 *
 * Phase 1: Stub (compiles, no functionality)
 * Phase 2: Implement using NuttX netdev API + BSD sockets
 *
 * Key API differences from lwIP:
 *   lwIP              | NuttX
 *   ------------------|-----------------------------------------
 *   netif_add()       | netdev_register() in net/netdev/netdev.h
 *   udp_new/bind/recv | BSD socket API (sys/socket.h)
 *   pbuf_alloc/free   | iob_alloc/free  (nuttx/net/iob.h)
 *   sys_timeout()     | wd_start()      (nuttx/wdog.h)
 ****************************************************************************/

/* TODO Phase 2: implement WireGuard netdev registration and UDP tunnel */
