/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.c
 *
 * NuttX network interface layer for WireGuard.
 *
 * This file replaces the upstream wireguardif.c (which requires lwIP APIs
 * not available in NuttX).  The mapping is:
 *
 *   lwIP (upstream)      | NuttX (this port)
 *   ---------------------|------------------------------------------
 *   netif_add()/output   | TUN character device (/dev/tun -> wg0)
 *   udp_new/bind/recv    | BSD UDP socket (sys/socket.h)
 *   pbuf                 | flat static buffers
 *   sys_timeout()        | poll() timeout in the daemon thread
 *
 * A single daemon thread multiplexes:
 *   - plaintext IP packets from the TUN device  -> encrypt -> UDP
 *   - encrypted UDP datagrams from the socket   -> decrypt -> TUN
 *   - the 400ms periodic protocol timer (handshakes, keepalive, rekey)
 *
 * The protocol/crypto engine (wireguard.c) is used unmodified.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <syslog.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <nuttx/net/tun.h>

#include "wireguard.h"
#include "crypto.h"
#include "nuttx-wireguardif.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WG_TIMER_MSECS   400
#define WG_IFNAME        "wg0"

/* Largest frame we handle: 16 byte transport header + MTU padded to a
 * 16 byte boundary + 16 byte auth tag, rounded up generously.
 */

#define WG_BUFSIZE       1600

#ifndef CONFIG_NET_WIREGUARD_DAEMON_STACKSIZE
#  define CONFIG_NET_WIREGUARD_DAEMON_STACKSIZE 8192
#endif

#ifndef CONFIG_NET_WIREGUARD_DAEMON_PRIORITY
#  define CONFIG_NET_WIREGUARD_DAEMON_PRIORITY 100
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This is "struct netif" as far as wireguard.h is concerned (it only
 * carries an opaque pointer).  It bundles the NuttX-side descriptors.
 */

struct netif
{
  char ifname[IFNAMSIZ];  /* TUN interface name (wg0) */
  int tun_fd;             /* TUN character device */
  int sock_fd;            /* UDP tunnel socket */
  bool link_up;           /* At least one peer has a valid session */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wireguard_device g_wg_device;
static struct netif g_wg_netif =
{
  .tun_fd  = -1,
  .sock_fd = -1,
};

static pthread_mutex_t g_wg_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_wg_running;   /* Daemon is up and serving */
static volatile bool g_wg_stop;      /* Ask the daemon to shut down */
static uint16_t g_wg_listen_port;

/* Start/stop synchronization with the daemon task.  The daemon task owns
 * all file descriptors: in NuttX, open files belong to the task group, so
 * they must not be opened by the transient "wg" command task (they would
 * be closed as soon as the command exits).
 */

static sem_t g_wg_ready_sem;
static sem_t g_wg_done_sem;
static int g_wg_start_result;

/* Startup parameters handed from wireguardif_start() to the daemon */

static struct
{
  char private_key[64];
  uint16_t listen_port;
  struct in_addr addr;
  struct in_addr netmask;
} g_wg_cfg;

/* Packet work areas - all access serialized by g_wg_lock */

static uint8_t g_tun_buf[WG_BUFSIZE];   /* plaintext from TUN */
static uint8_t g_udp_buf[WG_BUFSIZE];   /* ciphertext from UDP */
static uint8_t g_wrk_buf[WG_BUFSIZE];   /* encrypt/decrypt scratch */

/****************************************************************************
 * Private Functions: small helpers
 ****************************************************************************/

static inline bool ip_isany(const struct in_addr *a)
{
  return a->s_addr == 0;
}

static inline bool ip_netcmp(const struct in_addr *a,
                             const struct in_addr *b,
                             const struct in_addr *mask)
{
  return (a->s_addr & mask->s_addr) == (b->s_addr & mask->s_addr);
}

static void update_peer_addr(struct wireguard_peer *peer,
                             const struct in_addr *addr, uint16_t port)
{
  peer->ip   = *addr;
  peer->port = port;
}

/* Serialize source address+port (network byte order address, big-endian
 * port) for the cookie MAC2 calculation - see 5.4.7 of the paper.
 */

static size_t get_source_addr_port(const struct in_addr *addr,
                                   uint16_t port, uint8_t *buf,
                                   size_t buflen)
{
  size_t result = 0;

  if (buflen >= 4)
    {
      memcpy(buf, &addr->s_addr, 4); /* s_addr already network order */
      result += 4;
    }

  if (buflen >= result + 2)
    {
      buf[result]     = (uint8_t)(port >> 8);
      buf[result + 1] = (uint8_t)(port & 0xff);
      result += 2;
    }

  return result;
}

static struct wireguard_peer *
peer_lookup_by_allowed_ip(struct wireguard_device *device,
                          const struct in_addr *ipaddr)
{
  struct wireguard_peer *tmp;
  int x;
  int y;

  for (x = 0; x < WIREGUARD_MAX_PEERS; x++)
    {
      tmp = &device->peers[x];
      if (!tmp->valid)
        {
          continue;
        }

      for (y = 0; y < WIREGUARD_MAX_SRC_IPS; y++)
        {
          if (tmp->allowed_source_ips[y].valid &&
              ip_netcmp(ipaddr, &tmp->allowed_source_ips[y].ip,
                        &tmp->allowed_source_ips[y].mask))
            {
              return tmp;
            }
        }
    }

  return NULL;
}

static bool peer_add_ip(struct wireguard_peer *peer, struct in_addr ip,
                        struct in_addr mask)
{
  struct wireguard_allowed_ip *allowed;
  int x;

  /* Look for existing match first */

  for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
    {
      allowed = &peer->allowed_source_ips[x];
      if (allowed->valid && allowed->ip.s_addr == ip.s_addr &&
          allowed->mask.s_addr == mask.s_addr)
        {
          return true;
        }
    }

  /* Look for a free slot */

  for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
    {
      allowed = &peer->allowed_source_ips[x];
      if (!allowed->valid)
        {
          allowed->valid = true;
          allowed->ip    = ip;
          allowed->mask  = mask;
          return true;
        }
    }

  return false;
}

/****************************************************************************
 * Private Functions: output path (UDP send)
 ****************************************************************************/

static int wg_udp_send(struct netif *netif, const void *buf, size_t len,
                       const struct in_addr *addr, uint16_t port)
{
  struct sockaddr_in to;
  ssize_t n;

  memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port   = htons(port);
  to.sin_addr   = *addr;

  n = sendto(netif->sock_fd, buf, len, 0, (struct sockaddr *)&to,
             sizeof(to));
  return (n < 0) ? -errno : OK;
}

static int wireguardif_peer_output(struct netif *netif, const void *buf,
                                   size_t len, struct wireguard_peer *peer)
{
  /* Send to last known IP/port, not the configured connect IP/port */

  return wg_udp_send(netif, buf, len, &peer->ip, peer->port);
}

/* Encrypt one plaintext IP packet (or a keepalive when data==NULL/len==0)
 * for the given peer and transmit it inside a transport-data message.
 */

static int wireguardif_output_to_peer(struct netif *netif,
                                      const uint8_t *data, size_t len,
                                      struct wireguard_peer *peer)
{
  struct message_transport_data *hdr;
  struct wireguard_keypair *keypair = &peer->curr_keypair;
  size_t padded_len;
  size_t pkt_len;
  uint32_t now;
  uint8_t *dst;
  int result;

  /* We may not be able to use the current keypair if we haven't received
   * data yet - may need to resort to the previous keypair.
   */

  if (keypair->valid && !keypair->initiator && keypair->last_rx == 0)
    {
      keypair = &peer->prev_keypair;
    }

  if (!keypair->valid || !(keypair->initiator || keypair->last_rx != 0))
    {
      return -ENOTCONN;
    }

  if (wireguard_expired(keypair->keypair_millis, REJECT_AFTER_TIME) ||
      keypair->sending_counter >= REJECT_AFTER_MESSAGES)
    {
      keypair_destroy(keypair);
      return -ENOTCONN;
    }

  /* Round up to next 16 byte boundary; a keepalive is zero bytes */

  padded_len = (len + 15) & ~(size_t)15;
  pkt_len    = sizeof(struct message_transport_data) + padded_len +
               WIREGUARD_AUTHTAG_LEN;

  if (pkt_len > sizeof(g_wrk_buf))
    {
      return -EMSGSIZE;
    }

  memset(g_wrk_buf, 0, pkt_len);
  hdr = (struct message_transport_data *)g_wrk_buf;

  hdr->type     = MESSAGE_TRANSPORT_DATA;
  hdr->receiver = keypair->remote_index;
  U64TO8_LITTLE(hdr->counter, keypair->sending_counter);

  dst = &hdr->enc_packet[0];
  if (len > 0 && data)
    {
      memcpy(dst, data, len);
    }

  /* Note: encrypts in place and increments sending_counter */

  wireguard_encrypt_packet(dst, dst, padded_len, keypair);

  result = wireguardif_peer_output(netif, g_wrk_buf, pkt_len, peer);
  if (result == OK)
    {
      now = wireguard_sys_now();
      peer->last_tx    = now;
      keypair->last_tx = now;
    }

  /* Check to see if we should rekey */

  if (keypair->sending_counter >= REKEY_AFTER_MESSAGES ||
      (keypair->initiator &&
       wireguard_expired(keypair->keypair_millis, REKEY_AFTER_TIME)))
    {
      peer->send_handshake = true;
    }

  return result;
}

/* A plaintext IP packet has been read from the TUN device: route it to
 * the peer whose allowed-ips match the destination address.
 */

static int wireguardif_tun_output(struct netif *netif, const uint8_t *data,
                                  size_t len)
{
  struct wireguard_peer *peer;
  struct in_addr dest;

  /* Minimum sanity: IPv4 header, version 4 */

  if (len < 20 || (data[0] >> 4) != 4)
    {
      return -EPROTONOSUPPORT;
    }

  memcpy(&dest.s_addr, &data[16], 4);

  peer = peer_lookup_by_allowed_ip(&g_wg_device, &dest);
  if (!peer)
    {
      return -EHOSTUNREACH;
    }

  return wireguardif_output_to_peer(netif, data, len, peer);
}

static void wireguardif_send_keepalive(struct wireguard_device *device,
                                       struct wireguard_peer *peer)
{
  /* Send an empty packet as a keep-alive */

  wireguardif_output_to_peer(device->netif, NULL, 0, peer);
}

/****************************************************************************
 * Private Functions: handshake tx
 ****************************************************************************/

static int wg_start_handshake(struct netif *netif,
                              struct wireguard_peer *peer)
{
  struct message_handshake_initiation msg;
  int result;

  if (!wireguard_create_handshake_initiation(&g_wg_device, peer, &msg))
    {
      return -EINVAL;
    }

  result = wireguardif_peer_output(netif, &msg, sizeof(msg), peer);
  peer->send_handshake = false;
  peer->last_initiation_tx = wireguard_sys_now();
  memcpy(peer->handshake_mac1, msg.mac1, WIREGUARD_COOKIE_LEN);
  peer->handshake_mac1_valid = true;
  return result;
}

static void wireguardif_send_handshake_response(
    struct wireguard_device *device, struct wireguard_peer *peer)
{
  struct message_handshake_response packet;

  if (wireguard_create_handshake_response(device, peer, &packet))
    {
      wireguard_start_session(peer, false);
      wireguardif_peer_output(device->netif, &packet, sizeof(packet),
                              peer);
    }
}

static void wireguardif_send_handshake_cookie(
    struct wireguard_device *device, const uint8_t *mac1, uint32_t index,
    const struct in_addr *addr, uint16_t port)
{
  struct message_cookie_reply packet;
  uint8_t source_buf[18];
  size_t source_len;

  source_len = get_source_addr_port(addr, port, source_buf,
                                    sizeof(source_buf));

  wireguard_create_cookie_reply(device, &packet, mac1, index, source_buf,
                                source_len);
  wg_udp_send(device->netif, &packet, sizeof(packet), addr, port);
}

/****************************************************************************
 * Private Functions: rx path
 ****************************************************************************/

static bool wireguardif_check_initiation_message(
    struct wireguard_device *device,
    struct message_handshake_initiation *msg,
    const struct in_addr *addr, uint16_t port)
{
  uint8_t *data = (uint8_t *)msg;
  uint8_t source_buf[18];
  size_t source_len;
  bool result = false;

  if (wireguard_check_mac1(device, data,
                           sizeof(struct message_handshake_initiation) -
                           (2 * WIREGUARD_COOKIE_LEN), msg->mac1))
    {
      if (!wireguard_is_under_load())
        {
          /* If we aren't under load we only need mac1 to be correct */

          result = true;
        }
      else
        {
          /* Under load: also require a valid mac2 (cookie) */

          source_len = get_source_addr_port(addr, port, source_buf,
                                            sizeof(source_buf));
          result = wireguard_check_mac2(device, data,
              sizeof(struct message_handshake_initiation) -
              WIREGUARD_COOKIE_LEN, source_buf, source_len, msg->mac2);

          if (!result)
            {
              /* 5.3 Denial of Service Mitigation & Cookies */

              wireguardif_send_handshake_cookie(device, msg->mac1,
                                                msg->sender, addr, port);
            }
        }
    }

  return result;
}

static bool wireguardif_check_response_message(
    struct wireguard_device *device,
    struct message_handshake_response *msg,
    const struct in_addr *addr, uint16_t port)
{
  uint8_t *data = (uint8_t *)msg;
  uint8_t source_buf[18];
  size_t source_len;
  bool result = false;

  if (wireguard_check_mac1(device, data,
                           sizeof(struct message_handshake_response) -
                           (2 * WIREGUARD_COOKIE_LEN), msg->mac1))
    {
      if (!wireguard_is_under_load())
        {
          result = true;
        }
      else
        {
          source_len = get_source_addr_port(addr, port, source_buf,
                                            sizeof(source_buf));
          result = wireguard_check_mac2(device, data,
              sizeof(struct message_handshake_response) -
              WIREGUARD_COOKIE_LEN, source_buf, source_len, msg->mac2);

          if (!result)
            {
              wireguardif_send_handshake_cookie(device, msg->mac1,
                                                msg->sender, addr, port);
            }
        }
    }

  return result;
}

static void wireguardif_process_response_message(
    struct wireguard_device *device, struct wireguard_peer *peer,
    struct message_handshake_response *response,
    const struct in_addr *addr, uint16_t port)
{
  if (wireguard_process_handshake_response(device, peer, response))
    {
      update_peer_addr(peer, addr, port);
      wireguard_start_session(peer, true);
      wireguardif_send_keepalive(device, peer);
      device->netif->link_up = true;
    }
}

static void wireguardif_process_data_message(
    struct wireguard_device *device, struct wireguard_peer *peer,
    struct message_transport_data *data_hdr, size_t data_len,
    const struct in_addr *addr, uint16_t port)
{
  struct wireguard_keypair *keypair;
  struct in_addr src_ip;
  uint64_t nonce;
  uint8_t *src;
  size_t src_len;
  size_t decrypted_len;
  uint16_t header_len;
  uint32_t now;
  bool src_ok;
  int x;

  keypair = get_peer_keypair_for_idx(peer, data_hdr->receiver);
  if (!keypair)
    {
      /* Could not locate valid keypair for remote index */

      return;
    }

  if (!keypair->receiving_valid ||
      wireguard_expired(keypair->keypair_millis, REJECT_AFTER_TIME) ||
      keypair->sending_counter >= REJECT_AFTER_MESSAGES)
    {
      /* Refuse to use this session any more until a new handshake */

      keypair_destroy(keypair);
      return;
    }

  nonce   = U8TO64_LITTLE(data_hdr->counter);
  src     = &data_hdr->enc_packet[0];
  src_len = data_len;

  if (src_len < WIREGUARD_AUTHTAG_LEN || src_len > sizeof(g_wrk_buf))
    {
      return;
    }

  decrypted_len = src_len - WIREGUARD_AUTHTAG_LEN;

  if (!wireguard_decrypt_packet(g_wrk_buf, src, src_len, nonce, keypair))
    {
      return;
    }

  /* Since the packet authenticated correctly, the source of the outer
   * UDP/IP packet is used to update the endpoint for this peer.
   */

  update_peer_addr(peer, addr, port);

  now = wireguard_sys_now();
  keypair->last_rx = now;
  peer->last_rx    = now;

  /* Might need to shuffle next keypair --> current keypair */

  keypair_update(peer, keypair);

  /* Check to see if we should rekey */

  if (keypair->initiator &&
      wireguard_expired(keypair->keypair_millis,
                        REJECT_AFTER_TIME - peer->keepalive_interval -
                        REKEY_TIMEOUT))
    {
      peer->send_handshake = true;
    }

  device->netif->link_up = true;

  if (decrypted_len == 0)
    {
      /* This was a keep-alive packet */

      return;
    }

  /* Check for packet replay / dupes */

  if (!wireguard_check_replay(keypair, nonce))
    {
      return;
    }

  /* Check the source IP of the plaintext inner packet routes back to
   * this peer in the cryptokey routing table (IPv4 only).
   */

  src_ok     = false;
  header_len = 0xffff;

  if (decrypted_len >= 20 && (g_wrk_buf[0] >> 4) == 4)
    {
      memcpy(&src_ip.s_addr, &g_wrk_buf[12], 4);
      for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
        {
          if (peer->allowed_source_ips[x].valid &&
              ip_netcmp(&src_ip, &peer->allowed_source_ips[x].ip,
                        &peer->allowed_source_ips[x].mask))
            {
              src_ok = true;
              header_len = (uint16_t)((g_wrk_buf[2] << 8) | g_wrk_buf[3]);
              break;
            }
        }
    }

  if (src_ok && header_len >= 20 && header_len <= decrypted_len)
    {
      /* Inject the plaintext packet into the NuttX IP stack through
       * the TUN device.
       */

      ssize_t n = write(device->netif->tun_fd, g_wrk_buf, header_len);
      if (n < 0)
        {
          syslog(LOG_WARNING, "wireguard: tun write failed: %d\n", errno);
        }
    }
}

/* One encrypted UDP datagram has been received: dispatch by type */

static void wireguardif_network_rx(struct wireguard_device *device,
                                   uint8_t *data, size_t len,
                                   const struct in_addr *addr,
                                   uint16_t port)
{
  struct message_handshake_initiation *msg_initiation;
  struct message_handshake_response *msg_response;
  struct message_cookie_reply *msg_cookie;
  struct message_transport_data *msg_data;
  struct wireguard_peer *peer;
  uint8_t type;

  type = wireguard_get_message_type(data, len);

  switch (type)
    {
      case MESSAGE_HANDSHAKE_INITIATION:
        msg_initiation = (struct message_handshake_initiation *)data;

        if (wireguardif_check_initiation_message(device, msg_initiation,
                                                 addr, port))
          {
            peer = wireguard_process_initiation_message(device,
                                                        msg_initiation);
            if (peer)
              {
                update_peer_addr(peer, addr, port);
                wireguardif_send_handshake_response(device, peer);
              }
          }
        break;

      case MESSAGE_HANDSHAKE_RESPONSE:
        msg_response = (struct message_handshake_response *)data;

        if (wireguardif_check_response_message(device, msg_response,
                                               addr, port))
          {
            peer = peer_lookup_by_handshake(device,
                                            msg_response->receiver);
            if (peer)
              {
                wireguardif_process_response_message(device, peer,
                                                     msg_response, addr,
                                                     port);
              }
          }
        break;

      case MESSAGE_COOKIE_REPLY:
        msg_cookie = (struct message_cookie_reply *)data;

        peer = peer_lookup_by_handshake(device, msg_cookie->receiver);
        if (peer)
          {
            if (wireguard_process_cookie_message(device, peer,
                                                 msg_cookie))
              {
                /* Update the peer location; stay quiet until the next
                 * initiation message.
                 */

                update_peer_addr(peer, addr, port);
              }
          }
        break;

      case MESSAGE_TRANSPORT_DATA:
        msg_data = (struct message_transport_data *)data;

        peer = peer_lookup_by_receiver(device, msg_data->receiver);
        if (peer)
          {
            /* Header is 16 bytes long so take that off the length */

            wireguardif_process_data_message(
                device, peer, msg_data,
                len - sizeof(struct message_transport_data), addr, port);
          }
        break;

      default:

        /* Unknown or bad packet header */

        break;
    }
}

/****************************************************************************
 * Private Functions: periodic timer
 ****************************************************************************/

static bool wireguardif_can_send_initiation(struct wireguard_peer *peer)
{
  return (peer->last_initiation_tx == 0) ||
         wireguard_expired(peer->last_initiation_tx, REKEY_TIMEOUT);
}

static bool should_send_initiation(struct wireguard_peer *peer)
{
  bool result = false;

  if (wireguardif_can_send_initiation(peer))
    {
      if (peer->send_handshake)
        {
          result = true;
        }
      else if (peer->curr_keypair.valid && !peer->curr_keypair.initiator &&
               wireguard_expired(peer->curr_keypair.keypair_millis,
                                 REJECT_AFTER_TIME -
                                 peer->keepalive_interval))
        {
          result = true;
        }
      else if (!peer->curr_keypair.valid && peer->active)
        {
          result = true;
        }
    }

  return result;
}

static bool should_send_keepalive(struct wireguard_peer *peer)
{
  return (peer->keepalive_interval > 0) &&
         (peer->curr_keypair.valid || peer->prev_keypair.valid) &&
         wireguard_expired(peer->last_tx, peer->keepalive_interval);
}

static bool should_destroy_current_keypair(struct wireguard_peer *peer)
{
  return peer->curr_keypair.valid &&
         (wireguard_expired(peer->curr_keypair.keypair_millis,
                            REJECT_AFTER_TIME) ||
          peer->curr_keypair.sending_counter >= REJECT_AFTER_MESSAGES);
}

static bool should_reset_peer(struct wireguard_peer *peer)
{
  return peer->curr_keypair.valid &&
         wireguard_expired(peer->curr_keypair.keypair_millis,
                           REJECT_AFTER_TIME * 3);
}

static void wireguardif_tmr(struct wireguard_device *device)
{
  struct wireguard_peer *peer;
  bool link_up = false;
  int x;

  for (x = 0; x < WIREGUARD_MAX_PEERS; x++)
    {
      peer = &device->peers[x];
      if (!peer->valid)
        {
          continue;
        }

      if (should_reset_peer(peer))
        {
          /* Nothing back for too long - wipe out all crypto state and
           * revert back to the configured IP/port.
           */

          keypair_destroy(&peer->next_keypair);
          keypair_destroy(&peer->curr_keypair);
          keypair_destroy(&peer->prev_keypair);
          peer->ip   = peer->connect_ip;
          peer->port = peer->connect_port;
        }

      if (should_destroy_current_keypair(peer))
        {
          keypair_destroy(&peer->curr_keypair);
        }

      if (should_send_keepalive(peer))
        {
          wireguardif_send_keepalive(device, peer);
        }

      if (should_send_initiation(peer))
        {
          wg_start_handshake(device->netif, peer);
        }

      if (peer->curr_keypair.valid || peer->prev_keypair.valid)
        {
          link_up = true;
        }
    }

  device->netif->link_up = link_up;
}

/****************************************************************************
 * Private Functions: daemon task
 ****************************************************************************/

static void wireguardif_daemon_loop(void)
{
  struct wireguard_device *device = &g_wg_device;
  struct netif *netif = &g_wg_netif;
  struct pollfd fds[2];
  struct sockaddr_in from;
  socklen_t fromlen;
  struct in_addr addr;
  uint32_t last_tmr = wireguard_sys_now();
  ssize_t n;
  int ret;

  while (!g_wg_stop)
    {
      fds[0].fd      = netif->tun_fd;
      fds[0].events  = POLLIN;
      fds[0].revents = 0;
      fds[1].fd      = netif->sock_fd;
      fds[1].events  = POLLIN;
      fds[1].revents = 0;

      ret = poll(fds, 2, WG_TIMER_MSECS / 2);
      if (g_wg_stop)
        {
          break;
        }

      pthread_mutex_lock(&g_wg_lock);

      if (ret > 0 && (fds[0].revents & POLLIN) != 0)
        {
          /* Plaintext IP packet from the local stack */

          n = read(netif->tun_fd, g_tun_buf, sizeof(g_tun_buf));
          if (n > 0)
            {
              wireguardif_tun_output(netif, g_tun_buf, (size_t)n);
            }
        }

      if (ret > 0 && (fds[1].revents & POLLIN) != 0)
        {
          /* Encrypted datagram from the tunnel socket */

          fromlen = sizeof(from);
          n = recvfrom(netif->sock_fd, g_udp_buf, sizeof(g_udp_buf), 0,
                       (struct sockaddr *)&from, &fromlen);
          if (n > 0)
            {
              addr = from.sin_addr;
              wireguardif_network_rx(device, g_udp_buf, (size_t)n,
                                     &addr, ntohs(from.sin_port));
            }
        }

      /* Periodic protocol work */

      if ((uint32_t)(wireguard_sys_now() - last_tmr) >= WG_TIMER_MSECS)
        {
          wireguardif_tmr(device);
          last_tmr = wireguard_sys_now();
        }

      pthread_mutex_unlock(&g_wg_lock);
    }
}

/****************************************************************************
 * Private Functions: interface setup helpers
 ****************************************************************************/

static int wg_tun_create(struct netif *netif)
{
  struct ifreq ifr;
  int fd;
  int ret;

  fd = open("/dev/tun", O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN;
  strlcpy(ifr.ifr_name, WG_IFNAME, IFNAMSIZ);

  ret = ioctl(fd, TUNSETIFF, (unsigned long)&ifr);
  if (ret < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  strlcpy(netif->ifname, ifr.ifr_name, IFNAMSIZ);
  netif->tun_fd = fd;
  return OK;
}

static int wg_ifconfig(const char *ifname, struct in_addr addr,
                       struct in_addr netmask)
{
  struct sockaddr_in *inaddr;
  struct ifreq req;
  int sockfd;
  int ret;

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    {
      return -errno;
    }

  /* IP address */

  memset(&req, 0, sizeof(req));
  strlcpy(req.ifr_name, ifname, IFNAMSIZ);
  inaddr             = (struct sockaddr_in *)&req.ifr_addr;
  inaddr->sin_family = AF_INET;
  inaddr->sin_port   = 0;
  inaddr->sin_addr   = addr;

  ret = ioctl(sockfd, SIOCSIFADDR, (unsigned long)&req);
  if (ret < 0)
    {
      goto errout;
    }

  /* Netmask */

  memset(&req, 0, sizeof(req));
  strlcpy(req.ifr_name, ifname, IFNAMSIZ);
  inaddr             = (struct sockaddr_in *)&req.ifr_addr;
  inaddr->sin_family = AF_INET;
  inaddr->sin_port   = 0;
  inaddr->sin_addr   = netmask;

  ret = ioctl(sockfd, SIOCSIFNETMASK, (unsigned long)&req);
  if (ret < 0)
    {
      goto errout;
    }

  /* Interface UP */

  memset(&req, 0, sizeof(req));
  strlcpy(req.ifr_name, ifname, IFNAMSIZ);
  ret = ioctl(sockfd, SIOCGIFFLAGS, (unsigned long)&req);
  if (ret < 0)
    {
      goto errout;
    }

  req.ifr_flags |= IFF_UP;
  ret = ioctl(sockfd, SIOCSIFFLAGS, (unsigned long)&req);
  if (ret < 0)
    {
      goto errout;
    }

  close(sockfd);
  return OK;

errout:
  ret = -errno;
  close(sockfd);
  return ret;
}

static int wg_udp_open(uint16_t listen_port)
{
  struct sockaddr_in bindaddr;
  int fd;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      return -errno;
    }

  memset(&bindaddr, 0, sizeof(bindaddr));
  bindaddr.sin_family      = AF_INET;
  bindaddr.sin_port        = htons(listen_port);
  bindaddr.sin_addr.s_addr = INADDR_ANY;

  if (bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0)
    {
      int ret = -errno;
      close(fd);
      return ret;
    }

  return fd;
}

/* Set up TUN + socket + protocol state.  Runs inside the daemon task so
 * that the file descriptors belong to the daemon's task group.
 */

static int wireguardif_daemon_setup(void)
{
  struct wireguard_device *device = &g_wg_device;
  struct netif *netif = &g_wg_netif;
  uint8_t key[WIREGUARD_PRIVATE_KEY_LEN];
  size_t key_len = sizeof(key);
  int ret;

  if (!wireguard_base64_decode(g_wg_cfg.private_key, key, &key_len) ||
      key_len != WIREGUARD_PRIVATE_KEY_LEN)
    {
      return -EINVAL;
    }

  /* Initialise the WireGuard protocol engine (precomputed constants) */

  wireguard_init();

  memset(device, 0, sizeof(*device));

  ret = wg_tun_create(netif);
  if (ret < 0)
    {
      goto errout;
    }

  ret = wg_ifconfig(netif->ifname, g_wg_cfg.addr, g_wg_cfg.netmask);
  if (ret < 0)
    {
      goto errout_tun;
    }

  ret = wg_udp_open(g_wg_cfg.listen_port);
  if (ret < 0)
    {
      goto errout_tun;
    }

  netif->sock_fd = ret;
  netif->link_up = false;

  device->netif   = netif;
  device->udp_pcb = NULL;  /* Not used on NuttX - BSD socket instead */

  if (!wireguard_device_init(device, key))
    {
      ret = -EINVAL;
      goto errout_sock;
    }

  crypto_zero(key, sizeof(key));
  g_wg_listen_port = g_wg_cfg.listen_port;
  return OK;

errout_sock:
  close(netif->sock_fd);
  netif->sock_fd = -1;

errout_tun:
  close(netif->tun_fd);
  netif->tun_fd = -1;

errout:
  crypto_zero(key, sizeof(key));
  crypto_zero(device, sizeof(*device));
  return ret;
}

static void wireguardif_daemon_teardown(void)
{
  struct netif *netif = &g_wg_netif;

  if (netif->sock_fd >= 0)
    {
      close(netif->sock_fd);
      netif->sock_fd = -1;
    }

  if (netif->tun_fd >= 0)
    {
      /* Closing the TUN fd unregisters the network interface */

      close(netif->tun_fd);
      netif->tun_fd = -1;
    }

  /* Wipe all key material */

  crypto_zero(&g_wg_device, sizeof(g_wg_device));
}

static int wireguardif_daemon_task(int argc, char *argv[])
{
  int ret;

  ret = wireguardif_daemon_setup();

  g_wg_start_result = ret;
  if (ret < 0)
    {
      sem_post(&g_wg_ready_sem);
      return EXIT_FAILURE;
    }

  g_wg_running = true;
  sem_post(&g_wg_ready_sem);

  syslog(LOG_INFO, "wireguard: %s up, listening on udp/%u\n",
         g_wg_netif.ifname, g_wg_listen_port);

  wireguardif_daemon_loop();

  wireguardif_daemon_teardown();
  g_wg_running = false;

  syslog(LOG_INFO, "wireguard: stopped\n");
  sem_post(&g_wg_done_sem);
  return EXIT_SUCCESS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int wireguardif_start(const char *private_key, uint16_t listen_port,
                      struct in_addr addr, struct in_addr netmask)
{
  int pid;
  int ret;

  if (!private_key ||
      strlen(private_key) >= sizeof(g_wg_cfg.private_key))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_wg_lock);

  if (g_wg_running)
    {
      pthread_mutex_unlock(&g_wg_lock);
      return -EALREADY;
    }

  strlcpy(g_wg_cfg.private_key, private_key,
          sizeof(g_wg_cfg.private_key));
  g_wg_cfg.listen_port = listen_port;
  g_wg_cfg.addr        = addr;
  g_wg_cfg.netmask     = netmask;

  g_wg_stop = false;
  g_wg_start_result = OK;
  sem_init(&g_wg_ready_sem, 0, 0);
  sem_init(&g_wg_done_sem, 0, 0);

  /* The daemon must be a separate task: it owns the TUN and socket file
   * descriptors, which would be closed by the OS when the transient wg
   * command task exits.
   */

  pid = task_create("wireguard",
                    CONFIG_NET_WIREGUARD_DAEMON_PRIORITY,
                    CONFIG_NET_WIREGUARD_DAEMON_STACKSIZE,
                    wireguardif_daemon_task, NULL);
  if (pid < 0)
    {
      ret = -errno;
      crypto_zero(&g_wg_cfg, sizeof(g_wg_cfg));
      pthread_mutex_unlock(&g_wg_lock);
      return ret;
    }

  /* Wait until the daemon reports setup success/failure */

  while (sem_wait(&g_wg_ready_sem) < 0 && errno == EINTR);

  ret = g_wg_start_result;
  crypto_zero(&g_wg_cfg, sizeof(g_wg_cfg));

  pthread_mutex_unlock(&g_wg_lock);
  return ret;
}

int wireguardif_stop(void)
{
  pthread_mutex_lock(&g_wg_lock);

  if (!g_wg_running)
    {
      pthread_mutex_unlock(&g_wg_lock);
      return -ENODEV;
    }

  g_wg_stop = true;
  pthread_mutex_unlock(&g_wg_lock);

  /* Wait for the daemon to tear down and exit */

  while (sem_wait(&g_wg_done_sem) < 0 && errno == EINTR);

  return OK;
}

bool wireguardif_running(void)
{
  return g_wg_running;
}

const char *wireguardif_ifname(void)
{
  return g_wg_netif.ifname;
}

uint16_t wireguardif_listen_port(void)
{
  return g_wg_listen_port;
}

int wireguardif_get_public_key(char *buf, size_t buflen)
{
  size_t outlen = buflen;

  if (!g_wg_running || !g_wg_device.valid)
    {
      return -ENODEV;
    }

  if (!wireguard_base64_encode(g_wg_device.public_key,
                               WIREGUARD_PUBLIC_KEY_LEN, buf, &outlen))
    {
      return -EINVAL;
    }

  return OK;
}

void wireguardif_peer_config_init(struct wireguardif_peer_config *config)
{
  memset(config, 0, sizeof(*config));
  config->endpoint_port = WIREGUARDIF_DEFAULT_PORT;
  config->keep_alive    = WIREGUARDIF_KEEPALIVE_DEFAULT;
}

static int wg_lookup_peer(uint8_t peer_index, struct wireguard_peer **out)
{
  struct wireguard_peer *peer;

  if (!g_wg_running || !g_wg_device.valid)
    {
      return -ENODEV;
    }

  peer = peer_lookup_by_peer_index(&g_wg_device, peer_index);
  if (!peer)
    {
      return -EINVAL;
    }

  *out = peer;
  return OK;
}

int wireguardif_add_peer(const struct wireguardif_peer_config *config,
                         uint8_t *peer_index)
{
  struct wireguard_device *device = &g_wg_device;
  uint8_t public_key[WIREGUARD_PUBLIC_KEY_LEN];
  size_t public_key_len = sizeof(public_key);
  struct wireguard_peer *peer = NULL;
  int result = OK;

  if (peer_index)
    {
      *peer_index = WIREGUARDIF_INVALID_INDEX;
    }

  if (!config || !config->public_key)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_wg_lock);

  if (!g_wg_running)
    {
      pthread_mutex_unlock(&g_wg_lock);
      return -ENODEV;
    }

  if (!wireguard_base64_decode(config->public_key, public_key,
                               &public_key_len) ||
      public_key_len != WIREGUARD_PUBLIC_KEY_LEN)
    {
      pthread_mutex_unlock(&g_wg_lock);
      return -EINVAL;
    }

  /* See if the peer is already registered */

  peer = peer_lookup_by_pubkey(device, public_key);
  if (!peer)
    {
      peer = peer_alloc(device);
      if (!peer)
        {
          result = -ENOMEM;
          goto out;
        }

      if (!wireguard_peer_init(device, peer, public_key,
                               config->preshared_key))
        {
          peer = NULL;
          result = -EINVAL;
          goto out;
        }

      peer->connect_ip   = config->endpoint_ip;
      peer->connect_port = config->endpoint_port;
      peer->ip           = peer->connect_ip;
      peer->port         = peer->connect_port;

      if (config->keep_alive == WIREGUARDIF_KEEPALIVE_DEFAULT)
        {
          peer->keepalive_interval = KEEPALIVE_TIMEOUT;
        }
      else
        {
          peer->keepalive_interval = config->keep_alive;
        }

      peer_add_ip(peer, config->allowed_ip, config->allowed_mask);
    }

out:
  if (result == OK && peer_index && peer)
    {
      *peer_index = wireguard_peer_index(device, peer);
    }

  pthread_mutex_unlock(&g_wg_lock);
  return result;
}

int wireguardif_remove_peer(uint8_t peer_index)
{
  struct wireguard_peer *peer;
  int ret;

  pthread_mutex_lock(&g_wg_lock);
  ret = wg_lookup_peer(peer_index, &peer);
  if (ret == OK)
    {
      crypto_zero(peer, sizeof(struct wireguard_peer));
      peer->valid = false;
    }

  pthread_mutex_unlock(&g_wg_lock);
  return ret;
}

int wireguardif_update_endpoint(uint8_t peer_index, struct in_addr ip,
                                uint16_t port)
{
  struct wireguard_peer *peer;
  int ret;

  pthread_mutex_lock(&g_wg_lock);
  ret = wg_lookup_peer(peer_index, &peer);
  if (ret == OK)
    {
      peer->connect_ip   = ip;
      peer->connect_port = port;
    }

  pthread_mutex_unlock(&g_wg_lock);
  return ret;
}

int wireguardif_connect(uint8_t peer_index)
{
  struct wireguard_peer *peer;
  int ret;

  pthread_mutex_lock(&g_wg_lock);
  ret = wg_lookup_peer(peer_index, &peer);
  if (ret == OK)
    {
      if (!ip_isany(&peer->connect_ip) && peer->connect_port > 0)
        {
          /* Set the flags - the daemon timer sends the initiation */

          peer->active = true;
          peer->ip     = peer->connect_ip;
          peer->port   = peer->connect_port;
          peer->send_handshake = true;
        }
      else
        {
          ret = -EDESTADDRREQ;
        }
    }

  pthread_mutex_unlock(&g_wg_lock);
  return ret;
}

int wireguardif_disconnect(uint8_t peer_index)
{
  struct wireguard_peer *peer;
  int ret;

  pthread_mutex_lock(&g_wg_lock);
  ret = wg_lookup_peer(peer_index, &peer);
  if (ret == OK)
    {
      peer->active = false;
      keypair_destroy(&peer->next_keypair);
      keypair_destroy(&peer->curr_keypair);
      keypair_destroy(&peer->prev_keypair);
    }

  pthread_mutex_unlock(&g_wg_lock);
  return ret;
}

int wireguardif_peer_status(uint8_t peer_index,
                            struct wireguardif_peer_status *status)
{
  struct wireguard_peer *peer;
  size_t outlen;
  int ret;

  if (!status)
    {
      return -EINVAL;
    }

  memset(status, 0, sizeof(*status));

  pthread_mutex_lock(&g_wg_lock);
  ret = wg_lookup_peer(peer_index, &peer);
  if (ret == OK)
    {
      status->valid         = peer->valid;
      status->up            = peer->curr_keypair.valid ||
                              peer->prev_keypair.valid;
      status->endpoint_ip   = peer->ip;
      status->endpoint_port = peer->port;
      status->last_tx       = peer->last_tx;
      status->last_rx       = peer->last_rx;

      outlen = sizeof(status->public_key);
      wireguard_base64_encode(peer->public_key, WIREGUARD_PUBLIC_KEY_LEN,
                              status->public_key, &outlen);
    }

  pthread_mutex_unlock(&g_wg_lock);
  return ret;
}
