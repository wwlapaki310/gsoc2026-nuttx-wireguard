/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.h
 *
 * NuttX network interface layer for WireGuard - public API.
 *
 * This replaces the upstream wireguardif.h (lwIP based).  On NuttX the
 * WireGuard interface is realized as a TUN device (wg0) driven by a
 * daemon thread, with the encrypted tunnel carried over a BSD UDP
 * socket.
 ****************************************************************************/

#ifndef __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H
#define __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Default MTU for WireGuard is 1420 bytes */

#define WIREGUARDIF_MTU               (1420)

#define WIREGUARDIF_DEFAULT_PORT      (51820)
#define WIREGUARDIF_KEEPALIVE_DEFAULT (0xffff)
#define WIREGUARDIF_INVALID_INDEX     (0xff)

/* Configuration for one peer.  Initialize with
 * wireguardif_peer_config_init() and then fill in at least public_key,
 * allowed_ip and allowed_mask.
 */

struct wireguardif_peer_config
{
  const char *public_key;       /* Required: base64 public key of peer */
  const uint8_t *preshared_key; /* Optional: 32 bytes, NULL if unused */

  /* Allowed ip/netmask inside the tunnel (cryptokey routing) */

  struct in_addr allowed_ip;
  struct in_addr allowed_mask;

  /* Endpoint details (may be blank for a listen-only peer) */

  struct in_addr endpoint_ip;
  uint16_t endpoint_port;

  /* Keep-alive interval in seconds; 0 disables,
   * WIREGUARDIF_KEEPALIVE_DEFAULT selects the protocol default.
   */

  uint16_t keep_alive;
};

/* Runtime status of one peer */

struct wireguardif_peer_status
{
  bool valid;                   /* Peer slot in use */
  bool up;                      /* Valid session keys established */
  struct in_addr endpoint_ip;   /* Latest known endpoint */
  uint16_t endpoint_port;
  uint32_t last_tx;             /* wireguard_sys_now() of last data tx/rx, */
  uint32_t last_rx;             /* 0 if never                              */
  char public_key[48];          /* base64 public key of the peer */
};

/* Bring up the WireGuard interface:
 *  - creates the TUN network device (default name "wg0")
 *  - assigns addr/netmask and sets the interface UP
 *  - binds a UDP socket on listen_port for the encrypted tunnel
 *  - starts the WireGuard daemon thread
 *
 * private_key is the base64 encoded device private key.
 * Returns OK (0) on success, negated errno on failure.
 */

int wireguardif_start(const char *private_key, uint16_t listen_port,
                      struct in_addr addr, struct in_addr netmask);

/* Tear down the interface and daemon; wipes all key material */

int wireguardif_stop(void);

/* True between successful start and stop */

bool wireguardif_running(void);

/* Name of the underlying network interface (e.g. "wg0") */

const char *wireguardif_ifname(void);

/* UDP port the tunnel listens on */

uint16_t wireguardif_listen_port(void);

/* Base64 device public key (buf should be >= 48 bytes) */

int wireguardif_get_public_key(char *buf, size_t buflen);

/* Helper to initialise the peer config struct with defaults */

void wireguardif_peer_config_init(struct wireguardif_peer_config *config);

/* Add a new peer.  On success peer_index (if non-NULL) receives the
 * index used to reference this peer in the calls below.
 */

int wireguardif_add_peer(const struct wireguardif_peer_config *config,
                         uint8_t *peer_index);

/* Remove the given peer and wipe its keys */

int wireguardif_remove_peer(uint8_t peer_index);

/* Update the configured endpoint of the given peer */

int wireguardif_update_endpoint(uint8_t peer_index, struct in_addr ip,
                                uint16_t port);

/* Start trying to establish a session with the given peer (requires a
 * configured endpoint).  The handshake itself is driven by the daemon.
 */

int wireguardif_connect(uint8_t peer_index);

/* Stop trying to connect and wipe session keys */

int wireguardif_disconnect(uint8_t peer_index);

/* Query status of the given peer */

int wireguardif_peer_status(uint8_t peer_index,
                            struct wireguardif_peer_status *status);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H */
