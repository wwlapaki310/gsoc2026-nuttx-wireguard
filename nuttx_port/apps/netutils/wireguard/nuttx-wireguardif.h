/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.h
 *
 * Public API for WireGuard on NuttX.
 *
 * This replaces wireguardif.h (which assumes upstream lwIP) and provides
 * a POSIX-friendly interface that works with NuttX's BSD socket layer.
 *
 * Usage example (NSH app or startup task):
 *
 *   struct wg_device_config dev_cfg = {
 *     .listen_port = 51820,
 *   };
 *   wg_key_from_hex(dev_cfg.private_key, "private_key_hex...");
 *   inet_aton("10.0.1.1", &dev_cfg.address);
 *   inet_aton("255.255.255.0", &dev_cfg.netmask);
 *
 *   wg_netdev_init(&dev_cfg);
 *
 *   struct wg_peer_config peer = { ... };
 *   uint8_t pidx;
 *   wg_netdev_add_peer(&peer, &pidx);
 *   wg_netdev_connect(pidx);
 *
 ****************************************************************************/

#ifndef __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H
#define __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <netinet/in.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WG_DEFAULT_PORT      51820
#define WG_KEY_LEN           32     /* 256-bit keys */

/* Must match WIREGUARD_MAX_PEERS in wireguard-platform.h */

#define WG_MAX_PEERS         8

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* WireGuard device (interface) configuration */

struct wg_device_config
{
  uint8_t          private_key[WG_KEY_LEN]; /* 32-byte private key (raw) */
  uint16_t         listen_port;              /* UDP listen port */
  struct in_addr   address;                  /* wg0 IPv4 address */
  struct in_addr   netmask;                  /* wg0 netmask */
};

/* WireGuard peer configuration */

struct wg_peer_config
{
  uint8_t          public_key[WG_KEY_LEN];   /* Peer's 32-byte public key */
  uint8_t          preshared_key[WG_KEY_LEN];/* Preshared key (zero = none) */
  struct in_addr   endpoint_ip;               /* Peer endpoint IPv4 address */
  uint16_t         endpoint_port;             /* Peer endpoint port */
  struct in_addr   allowed_ip;                /* Allowed source IP (inner) */
  struct in_addr   allowed_mask;              /* Allowed source netmask */
  uint16_t         keepalive_interval;        /* Persistent keepalive seconds (0=off) */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * wg_netdev_init - Initialize the WireGuard virtual interface (wg0).
 *
 * Registers wg0 as a NuttX netdev, creates the UDP tunnel socket, and
 * starts the background receive thread. Must be called once at startup.
 *
 * Returns 0 on success, negative errno on failure.
 */

int wg_netdev_init(const struct wg_device_config *cfg);

/**
 * wg_netdev_add_peer - Add a peer to the WireGuard device.
 *
 * peer_idx is set to the allocated peer slot index on success.
 * Returns 0 on success, negative errno on failure.
 */

int wg_netdev_add_peer(const struct wg_peer_config *peer_cfg,
                        uint8_t *peer_idx);

/**
 * wg_netdev_connect - Initiate a WireGuard handshake with the given peer.
 *
 * Sends a Handshake Initiation message to the peer's endpoint.
 * Returns 0 on success, negative errno on failure.
 */

int wg_netdev_connect(uint8_t peer_idx);

/**
 * wg_netdev_shutdown - Tear down the WireGuard interface.
 *
 * Stops the receive thread, closes the UDP socket, and unregisters wg0.
 */

void wg_netdev_shutdown(void);

/**
 * wg_key_from_hex - Decode a 64-character hex string to a 32-byte key.
 *
 * Returns 0 on success, -EINVAL if the string is not valid hex.
 */

int wg_key_from_hex(uint8_t key[WG_KEY_LEN], const char *hex);

/**
 * wg_key_from_base64 - Decode a 44-character base64 string to a 32-byte key.
 *
 * Returns 0 on success, -EINVAL on decode error.
 */

int wg_key_from_base64(uint8_t key[WG_KEY_LEN], const char *b64);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H */
