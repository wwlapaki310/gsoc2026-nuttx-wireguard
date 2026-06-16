/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.c
 *
 * WireGuard virtual network interface for NuttX (Phase 3 — complete).
 *
 * Architecture
 * ------------
 *
 *   ┌─────────────────────────────────────┐
 *   │       NuttX IP stack (tun0/wg0)     │
 *   └──────────┬─────────────┬────────────┘
 *              │  read()     │  write()
 *              ▼             ▼
 *        wg_tx_thread    wg_rx_thread
 *              │             │
 *         encrypt()      decrypt()    ← wireguard.c
 *              │             │
 *          sendto()      write(tun_fd)
 *              │
 *   ┌──────────┴────────────────────────┐
 *   │      UDP socket (port 51820)      │
 *   └──────────┬────────────────────────┘
 *              │  recvfrom()
 *              ▼
 *        wg_rx_thread
 *
 * Flow detail
 * -----------
 *   TX (outgoing encrypted):
 *     read(tun_fd) → [IP packet]
 *     → lookup peer by dst IP (allowed_ip routing)
 *     → wireguard_encrypt_packet(plaintext, keypair)
 *     → build message_transport_data header
 *     → sendto(udp_fd, peer_endpoint)
 *
 *   RX (incoming decrypted):
 *     recvfrom(udp_fd) → [WireGuard UDP datagram]
 *     → wireguard_get_message_type()
 *     → handshake: process + send response
 *     → data: wireguard_decrypt_packet() → write(tun_fd)
 *
 * Requires: CONFIG_TUN=y (NuttX TUN virtual NIC driver)
 *           CONFIG_DEV_RANDOM=y (for platform functions)
 *           CONFIG_NET_UDP=y
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

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

/* WireGuard protocol core */

#include "wireguard.h"

/* Our public API */

#include "nuttx-wireguardif.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WG_TUN_DEVICE        "/dev/tun0"
#define WG_RX_STACKSIZE      4096
#define WG_TX_STACKSIZE      4096
#define WG_MAX_PACKET        2048

/* WireGuard message type constants (same as MESSAGE_* in wireguard.h) */

#define WG_TYPE_INIT         1
#define WG_TYPE_RESP         2
#define WG_TYPE_COOKIE       3
#define WG_TYPE_DATA         4

/* WireGuard transport data header size (fixed part) */

#define WG_TRANSPORT_HDR_LEN offsetof(struct message_transport_data, enc_packet)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wg_priv_s
{
  /* WireGuard protocol state */

  struct wireguard_device  wg_dev;

  /* UDP tunnel socket */

  int                      udp_fd;
  uint16_t                 listen_port;

  /* TUN file descriptor (wg0 inner interface) */

  int                      tun_fd;

  /* Per-peer sendto endpoint (parallel to wg_dev.peers[]) */

  struct sockaddr_in       peer_ep[WIREGUARD_MAX_PEERS];
  bool                     peer_ep_valid[WIREGUARD_MAX_PEERS];

  /* Inner IP address / netmask for wg0 */

  struct in_addr           addr;
  struct in_addr           netmask;

  /* RX thread (UDP receive + decrypt + inject to TUN) */

  pthread_t                rx_thread;

  /* TX thread (read from TUN + encrypt + UDP send) */

  pthread_t                tx_thread;

  bool                     running;
  pthread_mutex_t          lock;

  /* Shared buffers (protected by lock where noted) */

  uint8_t                  udp_rx_buf[WG_MAX_PACKET]; /* lock held */
  uint8_t                  plain_buf[WG_MAX_PACKET];  /* local to rx thread */
  uint8_t                  enc_buf[WG_MAX_PACKET];    /* local to tx thread */
  uint8_t                  tun_rx_buf[WG_MAX_PACKET]; /* local to tx thread */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wg_priv_s g_wg;
static bool             g_initialized;

/****************************************************************************
 * Public: Key Utilities
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

  if (!b64 || !wireguard_base64_decode(b64, key, &outlen) ||
      outlen != WG_KEY_LEN)
    {
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Private: UDP send helper
 ****************************************************************************/

static int wg_send_to_peer(struct wg_priv_s *priv, int idx,
                             const void *buf, size_t len)
{
  ssize_t n;

  if (idx < 0 || idx >= WIREGUARD_MAX_PEERS || !priv->peer_ep_valid[idx])
    {
      return -ENOENT;
    }

  n = sendto(priv->udp_fd, buf, len, 0,
             (const struct sockaddr *)&priv->peer_ep[idx],
             sizeof(struct sockaddr_in));

  return (n < 0) ? -errno : 0;
}

/****************************************************************************
 * Private: Peer lookup helpers
 ****************************************************************************/

/* Return array index for a peer pointer, or -1 */

static int wg_peer_idx(struct wg_priv_s *priv,
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

/* Find peer for a destination IP (cryptokey routing) */

static struct wireguard_peer *wg_peer_for_ip(struct wg_priv_s *priv,
                                               uint32_t dst_ip_ne)
{
  int i;
  int x;

  for (i = 0; i < WIREGUARD_MAX_PEERS; i++)
    {
      struct wireguard_peer *peer = &priv->wg_dev.peers[i];

      if (!peer->valid)
        {
          continue;
        }

      for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
        {
          struct wireguard_allowed_ip *a = &peer->allowed_source_ips[x];

          if (a->valid &&
              (dst_ip_ne & a->mask.s_addr) ==
              (a->ip.s_addr & a->mask.s_addr))
            {
              return peer;
            }
        }
    }

  return NULL;
}

/****************************************************************************
 * Private: TX — read from TUN, encrypt, send via UDP
 *
 * Handles one packet read from tun_fd.
 * Called from wg_tx_thread (no lock held on entry).
 ****************************************************************************/

static void wg_tx_one_packet(struct wg_priv_s *priv,
                               const uint8_t *pkt, size_t pkt_len)
{
  struct wireguard_peer  *peer;
  struct wireguard_keypair *kp;
  struct message_transport_data *msg;
  uint32_t dst_ip;
  uint64_t counter;
  size_t enc_len;
  int idx;
  int i;

  /* Extract destination IP from IPv4 header (d_buf[16..19]) */

  if (pkt_len < 20 || (pkt[0] & 0xf0) != 0x40)
    {
      return; /* not IPv4 */
    }

  memcpy(&dst_ip, pkt + 16, 4); /* network byte order, stays NBO */

  pthread_mutex_lock(&priv->lock);

  peer = wg_peer_for_ip(priv, dst_ip);
  if (!peer)
    {
      pthread_mutex_unlock(&priv->lock);
      syslog(LOG_DEBUG, "wg0: tx: no peer for dst IP\n");
      return;
    }

  kp = &peer->curr_keypair;
  if (!kp->valid)
    {
      /* No active session — trigger handshake and drop this packet.
       * The application will retransmit at higher layers. */

      idx = wg_peer_idx(priv, peer);
      syslog(LOG_INFO, "wg0: tx: no session, initiating handshake "
             "with peer %d\n", idx);

      if (idx >= 0)
        {
          struct message_handshake_initiation init_msg;
          if (wireguard_create_handshake_initiation(&priv->wg_dev, peer,
                                                     &init_msg))
            {
              wg_send_to_peer(priv, idx, &init_msg, sizeof(init_msg));
            }
        }

      pthread_mutex_unlock(&priv->lock);
      return;
    }

  /* Build WireGuard transport data message.
   * Layout: [4B type+reserved][4B receiver][8B counter LE][N ciphertext]
   *
   * receiver = keypair->remote_index (the index the peer uses for their RX).
   * Plaintext must be padded to a multiple of 16 bytes (WireGuard spec).
   * wireguard_encrypt_packet() encrypts in-place and appends the 16-byte
   * Poly1305 authentication tag. */

  msg = (struct message_transport_data *)priv->enc_buf;
  msg->type        = WG_TYPE_DATA;
  msg->reserved[0] = 0;
  msg->reserved[1] = 0;
  msg->reserved[2] = 0;
  msg->receiver    = kp->remote_index;  /* peer's receive keypair index */

  /* Encode counter as little-endian uint64 */

  counter = kp->sending_counter++;
  for (i = 0; i < 8; i++)
    {
      msg->counter[i] = (uint8_t)(counter >> (i * 8));
    }

  /* Copy plaintext to enc_buf after header, then pad to multiple of 16 */

  uint8_t *dst = msg->enc_packet;
  size_t padded_len = (pkt_len + 15u) & ~15u;

  if (WG_TRANSPORT_HDR_LEN + padded_len + WIREGUARD_AUTHTAG_LEN >
      sizeof(priv->enc_buf))
    {
      pthread_mutex_unlock(&priv->lock);
      syslog(LOG_WARNING, "wg0: tx packet too large (%zu)\n", pkt_len);
      return;
    }

  memcpy(dst, pkt, pkt_len);
  if (padded_len > pkt_len)
    {
      memset(dst + pkt_len, 0, padded_len - pkt_len);
    }

  /* Encrypt in-place; appends WIREGUARD_AUTHTAG_LEN bytes of Poly1305 tag */

  wireguard_encrypt_packet(dst, dst, padded_len, kp);
  enc_len = WG_TRANSPORT_HDR_LEN + padded_len + WIREGUARD_AUTHTAG_LEN;

  idx = wg_peer_idx(priv, peer);
  pthread_mutex_unlock(&priv->lock);

  if (idx >= 0)
    {
      wg_send_to_peer(priv, idx, priv->enc_buf, enc_len);
      syslog(LOG_DEBUG, "wg0: tx %zu bytes encrypted to peer %d\n",
             pkt_len, idx);
    }
}

/****************************************************************************
 * Private: RX handlers — called from wg_rx_thread (lock held)
 ****************************************************************************/

static void wg_rx_initiation(struct wg_priv_s *priv,
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

  idx = wg_peer_idx(priv, peer);

  /* Update peer endpoint to actual source address */

  if (idx >= 0)
    {
      priv->peer_ep[idx]       = *src;
      priv->peer_ep_valid[idx] = true;
    }

  if (!wireguard_create_handshake_response(&priv->wg_dev, peer, &resp))
    {
      syslog(LOG_ERR, "wg0: create_handshake_response failed\n");
      return;
    }

  /* Activate session as responder */

  wireguard_start_session(peer, false);

  if (idx >= 0)
    {
      wg_send_to_peer(priv, idx, &resp, sizeof(resp));
      syslog(LOG_INFO, "wg0: handshake init from %s → response sent"
             " (peer %d, session active)\n", inet_ntoa(src->sin_addr), idx);
    }
}

static void wg_rx_response(struct wg_priv_s *priv,
                             const uint8_t *buf, size_t len)
{
  struct message_handshake_response *msg =
    (struct message_handshake_response *)buf;
  struct wireguard_peer *peer;

  if (len < sizeof(*msg))
    {
      return;
    }

  peer = peer_lookup_by_handshake(&priv->wg_dev, msg->receiver);
  if (!peer)
    {
      syslog(LOG_WARNING, "wg0: handshake response: unknown index\n");
      return;
    }

  if (!wireguard_process_handshake_response(&priv->wg_dev, peer, msg))
    {
      syslog(LOG_WARNING, "wg0: handshake response verification failed\n");
      return;
    }

  /* Activate session as initiator */

  wireguard_start_session(peer, true);

  syslog(LOG_INFO, "wg0: session established with peer %d (initiator)\n",
         wg_peer_idx(priv, peer));
}

static void wg_rx_cookie(struct wg_priv_s *priv,
                           const uint8_t *buf, size_t len)
{
  struct message_cookie_reply *msg = (struct message_cookie_reply *)buf;
  struct wireguard_peer *peer;

  if (len < sizeof(*msg))
    {
      return;
    }

  peer = peer_lookup_by_handshake(&priv->wg_dev, msg->receiver);
  if (peer)
    {
      wireguard_process_cookie_message(&priv->wg_dev, peer, msg);
    }
}

static void wg_rx_transport(struct wg_priv_s *priv,
                              const uint8_t *buf, size_t len)
{
  struct message_transport_data *msg =
    (struct message_transport_data *)buf;
  struct wireguard_peer *peer;
  struct wireguard_keypair *kp;
  uint64_t counter;
  size_t enc_len;
  size_t plain_len;
  int i;

  if (len < WG_TRANSPORT_HDR_LEN + WIREGUARD_AUTHTAG_LEN)
    {
      return;
    }

  enc_len = len - WG_TRANSPORT_HDR_LEN;

  /* Decode counter (LE uint64) */

  counter = 0;
  for (i = 0; i < 8; i++)
    {
      counter |= (uint64_t)msg->counter[i] << (i * 8);
    }

  peer = peer_lookup_by_receiver(&priv->wg_dev, msg->receiver);
  if (!peer)
    {
      syslog(LOG_DEBUG, "wg0: rx transport: unknown receiver\n");
      return;
    }

  kp = get_peer_keypair_for_idx(peer, msg->receiver);
  if (!kp || !kp->valid)
    {
      syslog(LOG_DEBUG, "wg0: rx transport: no valid keypair\n");
      return;
    }

  if (!wireguard_check_replay(kp, counter))
    {
      syslog(LOG_WARNING, "wg0: replay detected (counter=%" PRIu64 ")\n",
             counter);
      return;
    }

  if (!wireguard_decrypt_packet(priv->plain_buf, msg->enc_packet,
                                 enc_len, counter, kp))
    {
      syslog(LOG_WARNING, "wg0: decrypt failed\n");
      return;
    }

  plain_len = enc_len - WIREGUARD_AUTHTAG_LEN;

  if (plain_len == 0)
    {
      return; /* keepalive */
    }

  syslog(LOG_DEBUG, "wg0: rx %zu bytes decrypted (peer %d)\n",
         plain_len, wg_peer_idx(priv, peer));

  /* Inject into NuttX IP stack via TUN fd.
   * Release the lock before write() to avoid blocking the lock. */

  pthread_mutex_unlock(&priv->lock);

  if (priv->tun_fd >= 0)
    {
      ssize_t n = write(priv->tun_fd, priv->plain_buf, plain_len);
      if (n < 0)
        {
          syslog(LOG_WARNING, "wg0: tun write: %d\n", errno);
        }
    }

  pthread_mutex_lock(&priv->lock);
}

/****************************************************************************
 * Private: RX thread — receives encrypted UDP, dispatches
 ****************************************************************************/

static void *wg_rx_thread(void *arg)
{
  struct wg_priv_s *priv = (struct wg_priv_s *)arg;
  struct sockaddr_in src;
  socklen_t srclen;
  ssize_t n;
  uint8_t msg_type;

  syslog(LOG_INFO, "wg0: rx thread started (UDP/%u)\n", priv->listen_port);

  while (priv->running)
    {
      srclen = sizeof(src);
      n = recvfrom(priv->udp_fd, priv->udp_rx_buf, sizeof(priv->udp_rx_buf),
                   0, (struct sockaddr *)&src, &srclen);

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

      msg_type = wireguard_get_message_type(priv->udp_rx_buf, (size_t)n);

      pthread_mutex_lock(&priv->lock);

      switch (msg_type)
        {
          case WG_TYPE_INIT:
            wg_rx_initiation(priv, priv->udp_rx_buf, (size_t)n, &src);
            break;

          case WG_TYPE_RESP:
            wg_rx_response(priv, priv->udp_rx_buf, (size_t)n);
            break;

          case WG_TYPE_COOKIE:
            wg_rx_cookie(priv, priv->udp_rx_buf, (size_t)n);
            break;

          case WG_TYPE_DATA:
            /* wg_rx_transport() temporarily releases and re-acquires lock */
            wg_rx_transport(priv, priv->udp_rx_buf, (size_t)n);
            break;

          default:
            syslog(LOG_DEBUG, "wg0: unknown msg type %u\n", msg_type);
            break;
        }

      pthread_mutex_unlock(&priv->lock);
    }

  syslog(LOG_INFO, "wg0: rx thread exiting\n");
  return NULL;
}

/****************************************************************************
 * Private: TX thread — reads from TUN, encrypts, sends via UDP
 ****************************************************************************/

static void *wg_tx_thread(void *arg)
{
  struct wg_priv_s *priv = (struct wg_priv_s *)arg;
  ssize_t n;

  syslog(LOG_INFO, "wg0: tx thread started\n");

  while (priv->running)
    {
      if (priv->tun_fd < 0)
        {
          usleep(100000); /* wait for TUN fd */
          continue;
        }

      n = read(priv->tun_fd, priv->tun_rx_buf, sizeof(priv->tun_rx_buf));

      if (n < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              continue;
            }

          if (priv->running)
            {
              syslog(LOG_ERR, "wg0: tun read: %d\n", errno);
            }

          break;
        }

      if (n > 0)
        {
          wg_tx_one_packet(priv, priv->tun_rx_buf, (size_t)n);
        }
    }

  syslog(LOG_INFO, "wg0: tx thread exiting\n");
  return NULL;
}

/****************************************************************************
 * Private: Configure wg0 (tun0) IP address via socket ioctl
 ****************************************************************************/

static int wg_configure_tun(const char *ifname, struct in_addr addr,
                              struct in_addr netmask)
{
  struct ifreq ifr;
  struct sockaddr_in *sin;
  int sock;
  int ret = 0;

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0)
    {
      return -errno;
    }

  /* Set interface address */

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  sin = (struct sockaddr_in *)&ifr.ifr_addr;
  sin->sin_family      = AF_INET;
  sin->sin_addr        = addr;

  if (ioctl(sock, SIOCSIFADDR, &ifr) < 0)
    {
      syslog(LOG_WARNING, "wg0: SIOCSIFADDR: %d\n", errno);
      ret = -errno;
      goto out;
    }

  /* Set netmask */

  memset(&ifr.ifr_netmask, 0, sizeof(ifr.ifr_netmask));
  sin = (struct sockaddr_in *)&ifr.ifr_netmask;
  sin->sin_family      = AF_INET;
  sin->sin_addr        = netmask;

  if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0)
    {
      syslog(LOG_WARNING, "wg0: SIOCSIFNETMASK: %d\n", errno);
    }

  /* Bring up */

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  ifr.ifr_flags = IFF_UP | IFF_RUNNING;

  if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
    {
      syslog(LOG_WARNING, "wg0: SIOCSIFFLAGS UP: %d\n", errno);
    }

out:
  close(sock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * wg_netdev_init - Initialize WireGuard interface.
 *
 * Steps:
 *   1. wireguard_init() + wireguard_device_init() with private key
 *   2. Open /dev/tun0 (CONFIG_TUN=y required)
 *   3. Configure tun0 IP address via ioctl
 *   4. Bind UDP socket to listen_port
 *   5. Start rx and tx threads
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
  g_wg.tun_fd      = -1;
  g_wg.listen_port = cfg->listen_port ? cfg->listen_port : WG_DEFAULT_PORT;
  g_wg.addr        = cfg->address;
  g_wg.netmask     = cfg->netmask;

  pthread_mutex_init(&g_wg.lock, NULL);

  /* Initialize WireGuard protocol state */

  wireguard_init();

  if (!wireguard_device_init(&g_wg.wg_dev, cfg->private_key))
    {
      syslog(LOG_ERR, "wg0: wireguard_device_init failed\n");
      ret = -EINVAL;
      goto err_mutex;
    }

  /* Open TUN virtual NIC.
   * NuttX requires CONFIG_TUN=y and tun_initialize() called at boot.
   * /dev/tun0 provides a read/write fd into the IP stack. */

  g_wg.tun_fd = open(WG_TUN_DEVICE, O_RDWR);
  if (g_wg.tun_fd < 0)
    {
      syslog(LOG_ERR, "wg0: open %s failed: %d — is CONFIG_TUN=y?\n",
             WG_TUN_DEVICE, errno);
      ret = -errno;
      goto err_mutex;
    }

  /* Configure tun0 with the WireGuard inner IP address */

  ret = wg_configure_tun("tun0", g_wg.addr, g_wg.netmask);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "wg0: tun address config failed (%d), "
             "run ifconfig manually\n", ret);
    }

  /* Bind UDP tunnel socket */

  g_wg.udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_wg.udp_fd < 0)
    {
      syslog(LOG_ERR, "wg0: socket: %d\n", errno);
      ret = -errno;
      goto err_tun;
    }

  opt = 1;
  setsockopt(g_wg.udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(g_wg.listen_port);

  if (bind(g_wg.udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      syslog(LOG_ERR, "wg0: bind UDP/%u: %d\n", g_wg.listen_port, errno);
      ret = -errno;
      goto err_udp;
    }

  /* Start threads */

  g_wg.running = true;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, WG_RX_STACKSIZE);

  ret = pthread_create(&g_wg.rx_thread, &attr, wg_rx_thread, &g_wg);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      syslog(LOG_ERR, "wg0: rx pthread_create: %d\n", ret);
      g_wg.running = false;
      ret = -ret;
      goto err_udp;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, WG_TX_STACKSIZE);

  ret = pthread_create(&g_wg.tx_thread, &attr, wg_tx_thread, &g_wg);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      syslog(LOG_ERR, "wg0: tx pthread_create: %d\n", ret);
      g_wg.running = false;
      pthread_join(g_wg.rx_thread, NULL);
      ret = -ret;
      goto err_udp;
    }

  g_initialized = true;
  syslog(LOG_INFO, "wg0: up — addr %s, UDP/%u\n",
         inet_ntoa(g_wg.addr), g_wg.listen_port);

  return 0;

err_udp:
  close(g_wg.udp_fd);
  g_wg.udp_fd = -1;

err_tun:
  close(g_wg.tun_fd);
  g_wg.tun_fd = -1;

err_mutex:
  pthread_mutex_destroy(&g_wg.lock);
  return ret;
}

/**
 * wg_netdev_add_peer - Add a WireGuard peer.
 */

int wg_netdev_add_peer(const struct wg_peer_config *peer_cfg,
                        uint8_t *peer_idx_out)
{
  static const uint8_t zero_psk[WG_KEY_LEN];
  struct wireguard_peer *peer;
  const uint8_t *psk;
  int i;
  int x;

  if (!g_initialized || !peer_cfg || !peer_idx_out)
    {
      return -EINVAL;
    }

  psk = (memcmp(peer_cfg->preshared_key, zero_psk, WG_KEY_LEN) != 0)
        ? peer_cfg->preshared_key : NULL;

  pthread_mutex_lock(&g_wg.lock);

  peer = peer_alloc(&g_wg.wg_dev);
  if (!peer)
    {
      pthread_mutex_unlock(&g_wg.lock);
      return -ENOMEM;
    }

  if (!wireguard_peer_init(&g_wg.wg_dev, peer, peer_cfg->public_key, psk))
    {
      pthread_mutex_unlock(&g_wg.lock);
      syslog(LOG_ERR, "wg0: wireguard_peer_init failed\n");
      return -EINVAL;
    }

  /* Endpoint stored in peer for send */

  peer->connect_ip.s_addr = peer_cfg->endpoint_ip.s_addr;
  peer->connect_port      = peer_cfg->endpoint_port;

  /* Keepalive */

  if (peer_cfg->keepalive_interval > 0)
    {
      peer->keepalive_interval = peer_cfg->keepalive_interval;
    }

  /* Allowed source IPs for cryptokey routing */

  for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
    {
      struct wireguard_allowed_ip *a = &peer->allowed_source_ips[x];

      if (!a->valid)
        {
          a->valid       = true;
          a->ip.s_addr   = peer_cfg->allowed_ip.s_addr;
          a->mask.s_addr = peer_cfg->allowed_mask.s_addr;
          break;
        }
    }

  i = wg_peer_idx(&g_wg, peer);

  /* Store sendto endpoint */

  memset(&g_wg.peer_ep[i], 0, sizeof(struct sockaddr_in));
  g_wg.peer_ep[i].sin_family = AF_INET;
  g_wg.peer_ep[i].sin_addr   = peer_cfg->endpoint_ip;
  g_wg.peer_ep[i].sin_port   = htons(peer_cfg->endpoint_port);
  g_wg.peer_ep_valid[i]      = true;

  *peer_idx_out = (uint8_t)i;

  pthread_mutex_unlock(&g_wg.lock);

  syslog(LOG_INFO, "wg0: peer %d added — endpoint %s:%u\n",
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

  if (!g_initialized || peer_idx >= WIREGUARD_MAX_PEERS)
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
 * wg_netdev_shutdown - Stop WireGuard interface.
 */

void wg_netdev_shutdown(void)
{
  if (!g_initialized)
    {
      return;
    }

  g_wg.running = false;

  /* Close sockets to unblock blocking calls */

  if (g_wg.udp_fd >= 0)
    {
      close(g_wg.udp_fd);
      g_wg.udp_fd = -1;
    }

  if (g_wg.tun_fd >= 0)
    {
      close(g_wg.tun_fd);
      g_wg.tun_fd = -1;
    }

  pthread_join(g_wg.rx_thread, NULL);
  pthread_join(g_wg.tx_thread, NULL);
  pthread_mutex_destroy(&g_wg.lock);

  memset(&g_wg, 0, sizeof(g_wg));
  g_initialized = false;

  syslog(LOG_INFO, "wg0: shutdown complete\n");
}
