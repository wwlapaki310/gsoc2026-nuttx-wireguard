/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.c
 *
 * WireGuard virtual network interface for NuttX (Phase 2).
 *
 * Architecture overview
 * ---------------------
 *
 *   [ IP stack (wg0 netdev) ]
 *          |          ^
 *     d_txavail   devif_input
 *          |          |
 *   [ wg_tx() ]  [ wg_rx_thread ]
 *          |          |
 *     wireguard.c (encrypt / decrypt)
 *          |          |
 *      sendto()    recvfrom()
 *          |          |
 *   [ UDP socket, port 51820 ]
 *
 * lwIP → NuttX API mapping
 * ------------------------
 *   lwIP (wireguardif.c)       NuttX replacement
 *   ─────────────────────      ─────────────────────────────────────
 *   struct netif               struct net_driver_s  (nuttx/net/netdev.h)
 *   netif_add()                netdev_register()              [Phase 3]
 *   ip_input(pbuf, netif)      devif_input(dev)               [Phase 3]
 *   netif_set_link_up()        netdev_carrier_on()            [Phase 3]
 *   udp_new() / udp_bind()     socket() / bind()
 *   udp_sendto(pcb, pbuf)      sendto(fd, buf, ...)
 *   sys_timeout() / timers     wd_start() / keepalive thread  [Phase 3]
 *   pbuf_alloc / pbuf_free     d_buf / iob_alloc              [Phase 3]
 *
 * Phase 2 scope (SIM target)
 * --------------------------
 * - wireguard_device initialization with real private key
 * - UDP socket bound to listen port
 * - pthread receive loop: full handshake (init + response + cookie) handling
 * - Transport data: decrypt and log (IP injection deferred to Phase 3)
 * - wireguard_encrypt_packet / wireguard_decrypt_packet verified working
 *
 * wireguard.c API notes (verified against smartalock/wireguard-lwip)
 * ------------------------------------------------------------------
 * - wireguard_get_message_type(data, len) → MESSAGE_* constant
 * - wireguard_process_initiation_message(device, msg) → struct wireguard_peer *
 * - wireguard_process_handshake_response(device, peer, msg) → bool
 * - wireguard_process_cookie_message(device, peer, msg) → bool
 * - wireguard_create_handshake_initiation(device, peer, dst) → bool
 * - wireguard_create_handshake_response(device, peer, dst) → bool
 * - wireguard_encrypt_packet(dst, src, src_len, keypair)
 * - wireguard_decrypt_packet(dst, src, src_len, counter, keypair) → bool
 * - peer_lookup_by_receiver(device, receiver) → struct wireguard_peer *
 * - get_peer_keypair_for_idx(peer, idx) → struct wireguard_keypair *
 * - wireguard_check_replay(keypair, seq) → bool
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

/* WireGuard protocol core (wireguard-lwip, OS-independent) */

#include "wireguard.h"

/* Our public API */

#include "nuttx-wireguardif.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WG_RX_STACKSIZE   4096
#define WG_MAX_PACKET     2048  /* max WireGuard UDP payload (bytes) */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Internal state for the wg0 interface */

struct wg_priv_s
{
  struct wireguard_device wg_dev;                        /* protocol state */

  int              udp_fd;                               /* tunnel socket */
  uint16_t         listen_port;

  /* Per-peer endpoint (parallel to wg_dev.peers[]) */

  struct sockaddr_in peer_ep[WIREGUARD_MAX_PEERS];
  bool               peer_ep_valid[WIREGUARD_MAX_PEERS];

  /* wg0 inner address */

  struct in_addr   addr;
  struct in_addr   netmask;

  /* Receive thread */

  pthread_t        rx_thread;
  bool             running;
  pthread_mutex_t  lock;

  /* Shared receive buffer (lock held while processing) */

  uint8_t          rx_buf[WG_MAX_PACKET];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wg_priv_s g_wg;
static bool             g_initialized;

/****************************************************************************
 * Public Functions — Key Utilities
 ****************************************************************************/

int wg_key_from_hex(uint8_t key[WG_KEY_LEN], const char *hex)
{
  unsigned int byte;
  size_t i;

  if (!hex || strlen(hex) != (size_t)(WG_KEY_LEN * 2))
    {
      return -EINVAL;
    }

  for (i = 0; i < WG_KEY_LEN; i++)
    {
      if (sscanf(hex + i * 2, "%02x", &byte) != 1)
        {
          return -EINVAL;
        }
      key[i] = (uint8_t)byte;
    }

  return 0;
}

int wg_key_from_base64(uint8_t key[WG_KEY_LEN], const char *b64)
{
  size_t outlen = WG_KEY_LEN;

  if (!b64)
    {
      return -EINVAL;
    }

  /* Use wireguard-lwip's built-in decoder */

  if (!wireguard_base64_decode(b64, key, &outlen) || outlen != WG_KEY_LEN)
    {
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Private Functions — UDP Send
 ****************************************************************************/

static int wg_send_to_peer(struct wg_priv_s *priv, int peer_idx,
                             const void *buf, size_t len)
{
  ssize_t n;

  if (peer_idx < 0 || peer_idx >= WIREGUARD_MAX_PEERS ||
      !priv->peer_ep_valid[peer_idx])
    {
      return -ENOENT;
    }

  n = sendto(priv->udp_fd, buf, len, 0,
             (const struct sockaddr *)&priv->peer_ep[peer_idx],
             sizeof(struct sockaddr_in));

  if (n < 0)
    {
      syslog(LOG_WARNING, "wg0: sendto peer %d: %d\n", peer_idx, errno);
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * Private Functions — Packet Processing
 ****************************************************************************/

/**
 * wg_peer_to_idx - Return the peer array index for a given peer pointer.
 */

static int wg_peer_to_idx(struct wg_priv_s *priv,
                            const struct wireguard_peer *peer)
{
  int i;

  for (i = 0; i < WIREGUARD_MAX_PEERS; i++)
    {
      if (&priv->wg_dev.peers[i] == peer)
        {
          return i;
        }
    }

  return -1;
}

/**
 * wg_handle_initiation - Process Handshake Initiation and reply.
 */

static void wg_handle_initiation(struct wg_priv_s *priv,
                                   const uint8_t *buf, size_t len,
                                   const struct sockaddr_in *src)
{
  struct message_handshake_initiation *msg =
    (struct message_handshake_initiation *)buf;
  struct message_handshake_response resp;
  struct wireguard_peer *peer;
  int idx;

  if (len < sizeof(*msg))
    {
      return;
    }

  peer = wireguard_process_initiation_message(&priv->wg_dev, msg);
  if (!peer)
    {
      syslog(LOG_WARNING, "wg0: bad handshake init from %s\n",
             inet_ntoa(src->sin_addr));
      return;
    }

  syslog(LOG_INFO, "wg0: valid handshake init from %s:%u\n",
         inet_ntoa(src->sin_addr), ntohs(src->sin_port));

  /* Update the peer's dynamic endpoint to the actual source */

  idx = wg_peer_to_idx(priv, peer);
  if (idx >= 0)
    {
      priv->peer_ep[idx]       = *src;
      priv->peer_ep_valid[idx] = true;
    }

  /* Send Handshake Response */

  if (!wireguard_create_handshake_response(&priv->wg_dev, peer, &resp))
    {
      syslog(LOG_ERR, "wg0: create_handshake_response failed\n");
      return;
    }

  if (idx >= 0)
    {
      wg_send_to_peer(priv, idx, &resp, sizeof(resp));
      syslog(LOG_INFO, "wg0: sent handshake response to peer %d\n", idx);
    }
}

/**
 * wg_handle_response - Process Handshake Response (we were the initiator).
 */

static void wg_handle_response(struct wg_priv_s *priv,
                                 const uint8_t *buf, size_t len,
                                 const struct sockaddr_in *src)
{
  struct message_handshake_response *msg =
    (struct message_handshake_response *)buf;
  struct wireguard_peer *peer;

  if (len < sizeof(*msg))
    {
      return;
    }

  /* Find peer by the receiver (our sender index stored in the response) */

  peer = peer_lookup_by_handshake(&priv->wg_dev, msg->receiver);
  if (!peer)
    {
      syslog(LOG_WARNING, "wg0: handshake response for unknown index\n");
      return;
    }

  if (!wireguard_process_handshake_response(&priv->wg_dev, peer, msg))
    {
      syslog(LOG_WARNING, "wg0: handshake response verification failed\n");
      return;
    }

  syslog(LOG_INFO, "wg0: session established with peer %d\n",
         wg_peer_to_idx(priv, peer));
}

/**
 * wg_handle_cookie - Process Cookie Reply.
 */

static void wg_handle_cookie(struct wg_priv_s *priv,
                               const uint8_t *buf, size_t len)
{
  struct message_cookie_reply *msg = (struct message_cookie_reply *)buf;
  struct wireguard_peer *peer;

  if (len < sizeof(*msg))
    {
      return;
    }

  peer = peer_lookup_by_handshake(&priv->wg_dev, msg->receiver);
  if (!peer)
    {
      return;
    }

  wireguard_process_cookie_message(&priv->wg_dev, peer, msg);
}

/**
 * wg_handle_transport - Decrypt and deliver a transport data packet.
 *
 * Encrypted packet layout:
 *   [4B type + receiver][8B counter][N bytes ciphertext+tag]
 *
 * Phase 2: decrypts and logs.
 * Phase 3: calls devif_input() to deliver to IP stack.
 */

static void wg_handle_transport(struct wg_priv_s *priv,
                                  const uint8_t *buf, size_t len)
{
  struct message_transport_data *msg = (struct message_transport_data *)buf;
  struct wireguard_peer *peer;
  struct wireguard_keypair *keypair;
  uint8_t plain[WG_MAX_PACKET];
  size_t enc_len;
  uint64_t counter;
  size_t i;

  if (len < sizeof(*msg))
    {
      return;
    }

  /* enc_packet[] is a flexible array after the 16-byte fixed header.
   * counter[8] is little-endian per WireGuard spec. */

  enc_len = len - offsetof(struct message_transport_data, enc_packet);

  counter = 0;
  for (i = 0; i < 8; i++)
    {
      counter |= (uint64_t)msg->counter[i] << (i * 8); /* LE → host */
    }

  /* Look up keypair by receiver index */

  peer = peer_lookup_by_receiver(&priv->wg_dev, msg->receiver);
  if (!peer)
    {
      syslog(LOG_DEBUG, "wg0: transport: unknown receiver 0x%08x\n",
             msg->receiver);
      return;
    }

  keypair = get_peer_keypair_for_idx(peer, msg->receiver);
  if (!keypair || !keypair->valid)
    {
      syslog(LOG_DEBUG, "wg0: transport: no valid keypair\n");
      return;
    }

  /* Replay protection */

  if (!wireguard_check_replay(keypair, counter))
    {
      syslog(LOG_WARNING, "wg0: transport: replay detected (counter=%" PRIu64
             ")\n", counter);
      return;
    }

  /* Decrypt */

  if (!wireguard_decrypt_packet(plain, msg->enc_packet, enc_len,
                                 counter, keypair))
    {
      syslog(LOG_WARNING, "wg0: transport: decrypt failed\n");
      return;
    }

  /* Poly1305 auth tag (16 bytes) is consumed by decrypt, not in plaintext */

  size_t plain_len = enc_len > WIREGUARD_AUTHTAG_LEN
                     ? enc_len - WIREGUARD_AUTHTAG_LEN : 0;

  if (plain_len == 0)
    {
      /* Keepalive — nothing to deliver */
      return;
    }

  syslog(LOG_INFO, "wg0: rx %zu bytes decrypted (peer %d)\n",
         plain_len, wg_peer_to_idx(priv, peer));

  /*
   * Phase 3 TODO: inject into NuttX IP stack.
   *
   *   net_lock();
   *   memcpy(g_wg_netdev.d_buf, plain, plain_len);
   *   g_wg_netdev.d_len = (unsigned int)plain_len;
   *   devif_input(&g_wg_netdev);
   *   net_unlock();
   *
   * This requires wg0 to be registered via netdev_register() with a
   * net_driver_s and NET_LL_TUN link type — deferred to Phase 3.
   */
}

/**
 * wg_handle_rx_packet - Dispatch one received UDP datagram.
 */

static void wg_handle_rx_packet(struct wg_priv_s *priv,
                                  const uint8_t *buf, size_t len,
                                  const struct sockaddr_in *src)
{
  uint8_t msg_type = wireguard_get_message_type(buf, len);

  switch (msg_type)
    {
      case MESSAGE_HANDSHAKE_INITIATION:
        wg_handle_initiation(priv, buf, len, src);
        break;

      case MESSAGE_HANDSHAKE_RESPONSE:
        wg_handle_response(priv, buf, len, src);
        break;

      case MESSAGE_COOKIE_REPLY:
        wg_handle_cookie(priv, buf, len);
        break;

      case MESSAGE_TRANSPORT_DATA:
        wg_handle_transport(priv, buf, len);
        break;

      default:
        syslog(LOG_DEBUG, "wg0: unknown message type %u\n", msg_type);
        break;
    }
}

/**
 * wg_rx_thread - Receive encrypted UDP packets and dispatch.
 */

static void *wg_rx_thread(void *arg)
{
  struct wg_priv_s *priv = (struct wg_priv_s *)arg;
  struct sockaddr_in src;
  socklen_t srclen;
  ssize_t n;

  syslog(LOG_INFO, "wg0: rx thread started (UDP port %u)\n",
         priv->listen_port);

  while (priv->running)
    {
      srclen = sizeof(src);
      n = recvfrom(priv->udp_fd, priv->rx_buf, sizeof(priv->rx_buf), 0,
                   (struct sockaddr *)&src, &srclen);

      if (n < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              continue;
            }

          if (priv->running)
            {
              syslog(LOG_ERR, "wg0: recvfrom: %d\n", errno);
            }

          break;
        }

      pthread_mutex_lock(&priv->lock);
      wg_handle_rx_packet(priv, priv->rx_buf, (size_t)n, &src);
      pthread_mutex_unlock(&priv->lock);
    }

  syslog(LOG_INFO, "wg0: rx thread exiting\n");
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * wg_netdev_init - Initialize the WireGuard interface.
 *
 * 1. Call wireguard_init() to initialize global state in wireguard.c.
 * 2. Call wireguard_device_init() with our private key.
 * 3. Bind a UDP socket to the listen port.
 * 4. Start the receive thread.
 *
 * Phase 3: additionally register wg0 as a net_driver_s via
 * netdev_register(NET_LL_TUN) and call netdev_carrier_on().
 */

int wg_netdev_init(const struct wg_device_config *cfg)
{
  struct sockaddr_in addr;
  pthread_attr_t attr;
  int ret;
  int opt;

  if (g_initialized)
    {
      return -EALREADY;
    }

  if (!cfg)
    {
      return -EINVAL;
    }

  memset(&g_wg, 0, sizeof(g_wg));
  g_wg.udp_fd      = -1;
  g_wg.listen_port = cfg->listen_port ? cfg->listen_port : WG_DEFAULT_PORT;
  g_wg.addr        = cfg->address;
  g_wg.netmask     = cfg->netmask;

  pthread_mutex_init(&g_wg.lock, NULL);

  /* Global one-time init (sets up Noise protocol tables) */

  wireguard_init();

  /* Initialize device with private key — derives public key internally */

  if (!wireguard_device_init(&g_wg.wg_dev, cfg->private_key))
    {
      syslog(LOG_ERR, "wg0: wireguard_device_init failed (invalid key?)\n");
      ret = -EINVAL;
      goto err_mutex;
    }

  /* Open UDP tunnel socket */

  g_wg.udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_wg.udp_fd < 0)
    {
      syslog(LOG_ERR, "wg0: socket: %d\n", errno);
      ret = -errno;
      goto err_mutex;
    }

  opt = 1;
  setsockopt(g_wg.udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(g_wg.listen_port);

  if (bind(g_wg.udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      syslog(LOG_ERR, "wg0: bind port %u: %d\n", g_wg.listen_port, errno);
      ret = -errno;
      goto err_socket;
    }

  /* Start receive thread */

  g_wg.running = true;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, WG_RX_STACKSIZE);

  ret = pthread_create(&g_wg.rx_thread, &attr, wg_rx_thread, &g_wg);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      syslog(LOG_ERR, "wg0: pthread_create: %d\n", ret);
      g_wg.running = false;
      ret = -ret;
      goto err_socket;
    }

  g_initialized = true;
  syslog(LOG_INFO, "wg0: up, listening on UDP/%u\n", g_wg.listen_port);
  return 0;

err_socket:
  close(g_wg.udp_fd);
  g_wg.udp_fd = -1;

err_mutex:
  pthread_mutex_destroy(&g_wg.lock);
  return ret;
}

/**
 * wg_netdev_add_peer - Add a WireGuard peer.
 *
 * Allocates a slot in wg_dev.peers[] and fills:
 * - public key / preshared key
 * - endpoint (connect_ip, connect_port)
 * - allowed source IPs for cryptokey routing
 */

int wg_netdev_add_peer(const struct wg_peer_config *peer_cfg,
                        uint8_t *peer_idx_out)
{
  struct wireguard_peer *peer;
  uint8_t null_psk[WG_KEY_LEN];
  const uint8_t *psk;
  int i;
  int x;

  if (!g_initialized || !peer_cfg || !peer_idx_out)
    {
      return -EINVAL;
    }

  memset(null_psk, 0, sizeof(null_psk));
  psk = (memcmp(peer_cfg->preshared_key, null_psk, WG_KEY_LEN) != 0)
        ? peer_cfg->preshared_key : NULL;

  pthread_mutex_lock(&g_wg.lock);

  /* Allocate peer slot via wireguard.c helper */

  peer = peer_alloc(&g_wg.wg_dev);
  if (!peer)
    {
      pthread_mutex_unlock(&g_wg.lock);
      syslog(LOG_ERR, "wg0: no free peer slots (max %d)\n",
             WIREGUARD_MAX_PEERS);
      return -ENOMEM;
    }

  if (!wireguard_peer_init(&g_wg.wg_dev, peer,
                            peer_cfg->public_key, psk))
    {
      pthread_mutex_unlock(&g_wg.lock);
      syslog(LOG_ERR, "wg0: wireguard_peer_init failed\n");
      return -EINVAL;
    }

  /* Set connect endpoint (stored in peer struct) */

  peer->connect_ip.s_addr = peer_cfg->endpoint_ip.s_addr;
  peer->connect_port      = peer_cfg->endpoint_port;

  /* Keepalive */

  if (peer_cfg->keepalive_interval > 0)
    {
      peer->keepalive_interval = peer_cfg->keepalive_interval;
    }

  /* Set allowed source IPs for cryptokey routing */

  for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
    {
      struct wireguard_allowed_ip *a = &peer->allowed_source_ips[x];
      if (!a->valid)
        {
          a->valid    = true;
          a->ip.s_addr   = peer_cfg->allowed_ip.s_addr;
          a->mask.s_addr = peer_cfg->allowed_mask.s_addr;
          break;
        }
    }

  /* Get index and store endpoint for sendto() */

  i = wg_peer_to_idx(&g_wg, peer);

  memset(&g_wg.peer_ep[i], 0, sizeof(struct sockaddr_in));
  g_wg.peer_ep[i].sin_family      = AF_INET;
  g_wg.peer_ep[i].sin_addr        = peer_cfg->endpoint_ip;
  g_wg.peer_ep[i].sin_port        = htons(peer_cfg->endpoint_port);
  g_wg.peer_ep_valid[i]           = true;

  *peer_idx_out = (uint8_t)i;

  pthread_mutex_unlock(&g_wg.lock);

  syslog(LOG_INFO, "wg0: peer %d added, endpoint %s:%u\n",
         i, inet_ntoa(peer_cfg->endpoint_ip), peer_cfg->endpoint_port);
  return 0;
}

/**
 * wg_netdev_connect - Send Handshake Initiation to peer.
 */

int wg_netdev_connect(uint8_t peer_idx)
{
  struct wireguard_peer *peer;
  struct message_handshake_initiation msg;
  int ret;

  if (!g_initialized)
    {
      return -ENXIO;
    }

  if (peer_idx >= WIREGUARD_MAX_PEERS)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_wg.lock);

  peer = &g_wg.wg_dev.peers[peer_idx];
  if (!peer->valid)
    {
      pthread_mutex_unlock(&g_wg.lock);
      return -ENOENT;
    }

  if (!wireguard_create_handshake_initiation(&g_wg.wg_dev, peer, &msg))
    {
      pthread_mutex_unlock(&g_wg.lock);
      syslog(LOG_ERR, "wg0: create_handshake_initiation failed\n");
      return -EIO;
    }

  ret = wg_send_to_peer(&g_wg, (int)peer_idx, &msg, sizeof(msg));

  pthread_mutex_unlock(&g_wg.lock);

  if (ret == 0)
    {
      syslog(LOG_INFO, "wg0: handshake init sent to peer %u\n", peer_idx);
    }

  return ret;
}

/**
 * wg_netdev_shutdown - Stop the WireGuard interface.
 */

void wg_netdev_shutdown(void)
{
  if (!g_initialized)
    {
      return;
    }

  g_wg.running = false;

  /* Close socket to unblock recvfrom() */

  if (g_wg.udp_fd >= 0)
    {
      close(g_wg.udp_fd);
      g_wg.udp_fd = -1;
    }

  pthread_join(g_wg.rx_thread, NULL);
  pthread_mutex_destroy(&g_wg.lock);

  memset(&g_wg, 0, sizeof(g_wg));
  g_initialized = false;

  syslog(LOG_INFO, "wg0: shutdown complete\n");
}
