/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* NuttX network interface layer for WireGuard.
 *
 * This file replaces wireguardif.c from upstream wireguard-lwip, which is
 * written against lwIP's netif/pbuf/udp_pcb APIs that do not exist in
 * NuttX. NuttX has its own network stack (net/), so "wg0" is implemented
 * here as a NET_LL_TUN netdev (see drivers/net/tun.c for the pattern this
 * follows) whose "wire" is a plain UDP socket instead of physical
 * hardware:
 *
 *   plaintext IP packet (from NuttX stack, via d_txavail/devif_poll)
 *     -> wireguard_encrypt_packet() -> psock_sendto() on the UDP socket
 *
 *   UDP datagram (via a background psock_recvfrom() task)
 *     -> wireguard_decrypt_packet() -> injected back into the NuttX stack
 *        with ipv4_input()
 *
 * The protocol/crypto core (wireguard.c, crypto/) is unmodified upstream
 * code and knows nothing about any of this; this file only replaces the
 * lwIP glue that wireguardif.c used to provide.
 *
 * Only one wg netif ("wg0") is supported, matching the single-peer,
 * single-device limits already baked into wireguard-platform.h
 * (WIREGUARD_MAX_PEERS / WIREGUARD_MAX_SRC_IPS).
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <poll.h>
#include <debug.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <nuttx/clock.h>
#include <nuttx/semaphore.h>
#include <nuttx/wqueue.h>
#include <nuttx/mm/iob.h>
#include <nuttx/net/net.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/netdev.h>

#include <netutils/netlib.h>

#include "wireguard.h"
#include "crypto.h"
#include "nuttx-wireguardif.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WG_TXWORK           LPWORK

/* How long wg_rx_task() blocks waiting for traffic before waking up to run
 * the protocol timers anyway. Handshake and keep-alive deadlines are all
 * expiry-timestamp driven (see the wireguard_expired() checks in
 * wg_run_timers()), so this only bounds timer granularity, not accuracy.
 */

#define WG_TIMER_MSECS      400

/* Largest plaintext IP packet we will handle. Must be >= WIREGUARDIF_MTU
 * (1420) plus a little slack; matches the usual NET_TUN_PKTSIZE default.
 */

#define WG_MAX_PACKET       1500
#define WG_CRYPT_BUFSIZE    (16 + WG_MAX_PACKET + WIREGUARD_AUTHTAG_LEN)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wg_netdev_s
{
  struct net_driver_s dev;      /* Interface understood by the network */
  struct wireguard_device wg;   /* WireGuard protocol/device state */

  bool bifup;                   /* true: ifup false: ifdown */
  bool running;                 /* Background thread should keep running */
  bool registered;              /* wg_initialize() has already run */

  /* UDP socket used as the "wire", held as a raw struct socket rather
   * than a task file descriptor. sendto() on this socket happens from
   * several different task contexts (wg_rx_task, and the LPWORK thread
   * via wg_txavail_work() -> wg_txpoll()) that are not related to each
   * other by parent/child task_create() inheritance, and a plain fd is
   * only valid in the task (and its descendants) that owned it at
   * socket() time - calling sendto() on it from an unrelated task such
   * as the system LPWORK thread fails with EBADF. The psock_*() internal
   * API operates on this struct directly, with no file descriptor table
   * lookup at all, so it works uniformly from every context. See
   * wg_rx_task()'s comment for more detail.
   */

  struct socket psock;
  int rxtask;                   /* PID of the wg_rx_task() background task */
  struct work_s txwork;         /* Deferred TX poll work */

  /* Scratch buffers. All uses are serialised by the (recursive) network
   * lock: wg_txpoll() runs on the low priority work queue with net_lock()
   * held by its caller, and the RX thread takes net_lock() itself before
   * touching these, so the two never race.
   */

  uint8_t plainbuf[WG_MAX_PACKET];
  uint8_t cryptbuf[WG_CRYPT_BUFSIZE];

  /* Per-peer transfer counters for "wg show", indexed by
   * wireguard_peer_index(). Kept here rather than in struct wireguard_peer
   * so the vendored wireguard.h stays untouched.
   */

  uint64_t peer_rx_bytes[WIREGUARD_MAX_PEERS];
  uint64_t peer_tx_bytes[WIREGUARD_MAX_PEERS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR struct wireguard_peer *
  wg_peer_for_dest(FAR struct wg_netdev_s *priv, in_addr_t dest);
static bool wg_encrypt_and_send(FAR struct wg_netdev_s *priv,
                                 FAR struct wireguard_peer *peer,
                                 FAR const uint8_t *plaintext,
                                 size_t plaintext_len);
static void wg_send_keepalive(FAR struct wg_netdev_s *priv,
                               FAR struct wireguard_peer *peer);
static void wg_start_handshake(FAR struct wg_netdev_s *priv,
                                FAR struct wireguard_peer *peer);
static void wg_send_handshake_response(FAR struct wg_netdev_s *priv,
                                        FAR struct wireguard_peer *peer);

static void wg_inject_plaintext(FAR struct wg_netdev_s *priv,
                                 FAR const uint8_t *plaintext, size_t len);
static void wg_process_data_message(FAR struct wg_netdev_s *priv,
                                     FAR struct wireguard_peer *peer,
                                     FAR struct message_transport_data *hdr,
                                     size_t data_len, in_addr_t addr,
                                     uint16_t port);
static void wg_process_udp_packet(FAR struct wg_netdev_s *priv,
                                   FAR uint8_t *data, size_t len,
                                   in_addr_t addr, uint16_t port);
static void wg_run_timers(FAR struct wg_netdev_s *priv);
static int wg_rx_task(int argc, FAR char *argv[]);

static int  wg_txpoll(FAR struct net_driver_s *dev);
static void wg_txavail_work(FAR void *arg);
static int  wg_txavail(FAR struct net_driver_s *dev);
static int  wg_ifup(FAR struct net_driver_s *dev);
static int  wg_ifdown(FAR struct net_driver_s *dev);

static void wg_configure_peer(FAR struct wg_netdev_s *priv);
static void wg_configure_address(FAR struct wg_netdev_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wg_netdev_s g_wg;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wg_peer_for_dest
 *
 * Description:
 *   Find the peer whose allowed-ips cover the given (network byte order)
 *   destination address. Equivalent to peer_lookup_by_allowed_ip() in
 *   upstream wireguardif.c.
 *
 ****************************************************************************/

static FAR struct wireguard_peer *
  wg_peer_for_dest(FAR struct wg_netdev_s *priv, in_addr_t dest)
{
  int x;
  int y;

  for (x = 0; x < WIREGUARD_MAX_PEERS; x++)
    {
      FAR struct wireguard_peer *peer = &priv->wg.peers[x];

      if (!peer->valid)
        {
          continue;
        }

      for (y = 0; y < WIREGUARD_MAX_SRC_IPS; y++)
        {
          FAR struct wireguard_allowed_ip *allowed =
              &peer->allowed_source_ips[y];

          if (allowed->valid &&
              (dest & allowed->mask.s_addr) ==
              (allowed->ip.s_addr & allowed->mask.s_addr))
            {
              return peer;
            }
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: wg_encrypt_and_send
 *
 * Description:
 *   Encrypt plaintext (a full IP packet, or NULL/0 for a keep-alive) as a
 *   WireGuard transport data message and send it to the peer's current
 *   endpoint. Equivalent to wireguardif_output_to_peer() in upstream
 *   wireguardif.c, but writes into a flat buffer and sendto()s it instead
 *   of building a pbuf and calling udp_sendto().
 *
 ****************************************************************************/

static bool wg_encrypt_and_send(FAR struct wg_netdev_s *priv,
                                 FAR struct wireguard_peer *peer,
                                 FAR const uint8_t *plaintext,
                                 size_t plaintext_len)
{
  FAR struct wireguard_keypair *keypair = &peer->curr_keypair;
  FAR struct message_transport_data *hdr;
  struct sockaddr_in addr;
  size_t padded_len;
  size_t total_len;
  uint32_t now;
  ssize_t ret_sendto;
  bool ok = false;

  /* We may not be able to use the current keypair if we haven't received
   * data on it yet - fall back to the previous one, matching upstream.
   */

  if (keypair->valid && !keypair->initiator && keypair->last_rx == 0)
    {
      keypair = &peer->prev_keypair;
    }

  if (!keypair->valid || (!keypair->initiator && keypair->last_rx == 0))
    {
      return false;
    }

  if (wireguard_expired(keypair->keypair_millis, REJECT_AFTER_TIME) ||
      keypair->sending_counter >= REJECT_AFTER_MESSAGES)
    {
      keypair_destroy(keypair);
      return false;
    }

  padded_len = (plaintext_len + 15) & ~((size_t)0x0f);
  total_len = 16 + padded_len + WIREGUARD_AUTHTAG_LEN;

  if (total_len > sizeof(priv->cryptbuf))
    {
      nerr("ERROR: wg packet too large (%zu bytes)\n", plaintext_len);
      return false;
    }

  memset(priv->cryptbuf, 0, total_len);

  hdr = (FAR struct message_transport_data *)priv->cryptbuf;
  hdr->type = MESSAGE_TRANSPORT_DATA;
  hdr->receiver = keypair->remote_index;
  U64TO8_LITTLE(hdr->counter, keypair->sending_counter);

  if (plaintext_len > 0 && plaintext != NULL)
    {
      memcpy(&priv->cryptbuf[16], plaintext, plaintext_len);
    }

  wireguard_encrypt_packet(&priv->cryptbuf[16], &priv->cryptbuf[16],
                            padded_len, keypair);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr = peer->ip;
  addr.sin_port = htons(peer->port);

  ret_sendto = psock_sendto(&priv->psock, priv->cryptbuf, total_len, 0,
                             (FAR struct sockaddr *)&addr, sizeof(addr));
  if (ret_sendto >= 0)
    {
      now = wireguard_sys_now();
      peer->last_tx = now;
      keypair->last_tx = now;

      /* Count the transport payload (ciphertext + auth tag), i.e. everything
       * past the 16 byte transport header. This matches what the RX side
       * adds to peer_rx_bytes in wg_process_data_message(), so the two
       * halves of "wg show"'s transfer line measure the same thing.
       */

      priv->peer_tx_bytes[wireguard_peer_index(&priv->wg, peer)] +=
          total_len - 16;
      ok = true;
    }

  if (keypair->sending_counter >= REKEY_AFTER_MESSAGES)
    {
      peer->send_handshake = true;
    }
  else if (keypair->initiator &&
           wireguard_expired(keypair->keypair_millis, REKEY_AFTER_TIME))
    {
      peer->send_handshake = true;
    }

  return ok;
}

static void wg_send_keepalive(FAR struct wg_netdev_s *priv,
                               FAR struct wireguard_peer *peer)
{
  wg_encrypt_and_send(priv, peer, NULL, 0);
}

/****************************************************************************
 * Name: wg_start_handshake
 *
 * Description:
 *   Equivalent to wireguard_start_handshake() in upstream wireguardif.c.
 *
 ****************************************************************************/

static void wg_start_handshake(FAR struct wg_netdev_s *priv,
                                FAR struct wireguard_peer *peer)
{
  struct message_handshake_initiation msg;
  struct sockaddr_in addr;

  if (!wireguard_create_handshake_initiation(&priv->wg, peer, &msg))
    {
      return;
    }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr = peer->ip;
  addr.sin_port = htons(peer->port);

  psock_sendto(&priv->psock, &msg, sizeof(msg), 0,
               (FAR struct sockaddr *)&addr, sizeof(addr));

  peer->send_handshake = false;
  peer->last_initiation_tx = wireguard_sys_now();
  memcpy(peer->handshake_mac1, msg.mac1, WIREGUARD_COOKIE_LEN);
  peer->handshake_mac1_valid = true;
}

static void wg_send_handshake_response(FAR struct wg_netdev_s *priv,
                                        FAR struct wireguard_peer *peer)
{
  struct message_handshake_response packet;
  struct sockaddr_in addr;

  if (!wireguard_create_handshake_response(&priv->wg, peer, &packet))
    {
      return;
    }

  wireguard_start_session(peer, false);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr = peer->ip;
  addr.sin_port = htons(peer->port);

  psock_sendto(&priv->psock, &packet, sizeof(packet), 0,
               (FAR struct sockaddr *)&addr, sizeof(addr));
}

/****************************************************************************
 * Name: wg_inject_plaintext
 *
 * Description:
 *   Hand a decrypted IP packet to the NuttX network stack on the wg0
 *   interface. Mirrors the iob handling in tun_write() (drivers/net/tun.c):
 *   allocate an iob, copy the plaintext in, call ipv4_input(). If that
 *   produces an immediate reply (e.g. the stack answering a ping to wg0's
 *   own address), encrypt and send it straight back out to the same peer.
 *
 *   Caller must already hold the network lock.
 *
 ****************************************************************************/

static void wg_inject_plaintext(FAR struct wg_netdev_s *priv,
                                 FAR const uint8_t *plaintext, size_t len)
{
  int ret;

  if (len == 0 || len > WG_MAX_PACKET || !priv->bifup)
    {
      return;
    }

  netdev_iob_release(&priv->dev);
  ret = netdev_iob_prepare(&priv->dev, false, 0);
  priv->dev.d_buf = NULL;
  if (ret < 0)
    {
      return;
    }

  ret = iob_trycopyin(priv->dev.d_iob, plaintext, len, 0, false);
  if (ret < 0)
    {
      netdev_iob_release(&priv->dev);
      return;
    }

  priv->dev.d_len = (uint16_t)len;

  ipv4_input(&priv->dev);

  if (priv->dev.d_len > 0 && priv->dev.d_len <= sizeof(priv->plainbuf))
    {
      FAR struct ipv4_hdr_s *rh;
      in_addr_t dest;
      FAR struct wireguard_peer *peer;
      size_t replylen = priv->dev.d_len;

      iob_copyout(priv->plainbuf, priv->dev.d_iob, replylen, 0);

      rh = (FAR struct ipv4_hdr_s *)priv->plainbuf;
      dest = net_ip4addr_conv32(rh->destipaddr);
      peer = wg_peer_for_dest(priv, dest);
      if (peer != NULL)
        {
          wg_encrypt_and_send(priv, peer, priv->plainbuf, replylen);
        }
    }

  netdev_iob_release(&priv->dev);
}

/****************************************************************************
 * Name: wg_process_data_message
 *
 * Description:
 *   Equivalent to wireguardif_process_data_message() in upstream
 *   wireguardif.c.
 *
 ****************************************************************************/

static void wg_process_data_message(FAR struct wg_netdev_s *priv,
                                     FAR struct wireguard_peer *peer,
                                     FAR struct message_transport_data *hdr,
                                     size_t data_len, in_addr_t addr,
                                     uint16_t port)
{
  FAR struct wireguard_keypair *keypair;
  uint64_t nonce;
  FAR uint8_t *src;
  uint32_t now;

  keypair = get_peer_keypair_for_idx(peer, hdr->receiver);
  if (keypair == NULL)
    {
      return;
    }

  if (!keypair->receiving_valid ||
      wireguard_expired(keypair->keypair_millis, REJECT_AFTER_TIME) ||
      keypair->sending_counter >= REJECT_AFTER_MESSAGES)
    {
      keypair_destroy(keypair);
      return;
    }

  if (data_len < WIREGUARD_AUTHTAG_LEN ||
      (data_len - WIREGUARD_AUTHTAG_LEN) > sizeof(priv->plainbuf))
    {
      return;
    }

  nonce = U8TO64_LITTLE(hdr->counter);
  src = &hdr->enc_packet[0];

  memset(priv->plainbuf, 0, sizeof(priv->plainbuf));
  if (!wireguard_decrypt_packet(priv->plainbuf, src, data_len, nonce,
                                 keypair))
    {
      return;
    }

  /* Packet authenticated - update peer's known endpoint from the outer
   * UDP/IP source, per the protocol spec.
   */

  peer->ip.s_addr = addr;
  peer->port = port;

  now = wireguard_sys_now();
  keypair->last_rx = now;
  peer->last_rx = now;
  priv->peer_rx_bytes[wireguard_peer_index(&priv->wg, peer)] += data_len;

  keypair_update(peer, keypair);

  if (keypair->initiator &&
      wireguard_expired(keypair->keypair_millis,
                         REJECT_AFTER_TIME - peer->keepalive_interval -
                         REKEY_TIMEOUT))
    {
      peer->send_handshake = true;
    }

  netdev_carrier_on(&priv->dev);

  if (data_len > WIREGUARD_AUTHTAG_LEN)
    {
      uint16_t pktlen = (uint16_t)(data_len - WIREGUARD_AUTHTAG_LEN);
      FAR struct ipv4_hdr_s *iphdr = (FAR struct ipv4_hdr_s *)priv->plainbuf;

      if (((iphdr->vhl >> 4) & 0x0f) == 4)
        {
          in_addr_t srcip = net_ip4addr_conv32(iphdr->srcipaddr);
          uint16_t totlen = (uint16_t)((iphdr->len[0] << 8) | iphdr->len[1]);
          bool src_ok = false;
          int x;

          for (x = 0; x < WIREGUARD_MAX_SRC_IPS; x++)
            {
              FAR struct wireguard_allowed_ip *allowed =
                  &peer->allowed_source_ips[x];

              if (allowed->valid &&
                  (srcip & allowed->mask.s_addr) ==
                  (allowed->ip.s_addr & allowed->mask.s_addr))
                {
                  src_ok = true;
                  break;
                }
            }

          if (src_ok && totlen <= pktlen &&
              wireguard_check_replay(keypair, nonce))
            {
              wg_inject_plaintext(priv, priv->plainbuf, totlen);
            }
        }
    }

  /* else: zero-length payload - this was just a keep-alive */
}

/****************************************************************************
 * Name: wg_process_udp_packet
 *
 * Description:
 *   Equivalent to wireguardif_network_rx() in upstream wireguardif.c.
 *   Caller must already hold the network lock.
 *
 *   Cookie/mac2 handling is intentionally omitted: wireguard_is_under_load()
 *   is hard-wired to false on this platform (see nuttx-platform.c), so
 *   upstream's mac2/cookie-reply path is unreachable dead code here. It can
 *   be revisited if this platform ever wants DoS-load signalling.
 *
 ****************************************************************************/

static void wg_process_udp_packet(FAR struct wg_netdev_s *priv,
                                   FAR uint8_t *data, size_t len,
                                   in_addr_t addr, uint16_t port)
{
  FAR struct wireguard_peer *peer;
  uint8_t type = wireguard_get_message_type(data, len);

  switch (type)
    {
      case MESSAGE_HANDSHAKE_INITIATION:
        {
          FAR struct message_handshake_initiation *msg =
              (FAR struct message_handshake_initiation *)data;

          if (len == sizeof(*msg) &&
              wireguard_check_mac1(&priv->wg, data,
                                   sizeof(*msg) -
                                   (2 * WIREGUARD_COOKIE_LEN),
                                   msg->mac1))
            {
              peer = wireguard_process_initiation_message(&priv->wg, msg);
              if (peer != NULL)
                {
                  peer->ip.s_addr = addr;
                  peer->port = port;
                  wg_send_handshake_response(priv, peer);
                }
            }
        }
        break;

      case MESSAGE_HANDSHAKE_RESPONSE:
        {
          FAR struct message_handshake_response *msg =
              (FAR struct message_handshake_response *)data;

          if (len == sizeof(*msg) &&
              wireguard_check_mac1(&priv->wg, data,
                                   sizeof(*msg) -
                                   (2 * WIREGUARD_COOKIE_LEN),
                                   msg->mac1))
            {
              peer = peer_lookup_by_handshake(&priv->wg, msg->receiver);
              if (peer != NULL &&
                  wireguard_process_handshake_response(&priv->wg, peer, msg))
                {
                  peer->ip.s_addr = addr;
                  peer->port = port;
                  wireguard_start_session(peer, true);
                  wg_send_keepalive(priv, peer);
                  netdev_carrier_on(&priv->dev);
                }
            }
        }
        break;

      case MESSAGE_COOKIE_REPLY:

        /* Unreachable: peers never receive a cookie reply because we never
         * ask for one (wireguard_is_under_load() is always false).
         */

        break;

      case MESSAGE_TRANSPORT_DATA:
        {
          FAR struct message_transport_data *msg =
              (FAR struct message_transport_data *)data;

          if (len > 16)
            {
              peer = peer_lookup_by_receiver(&priv->wg, msg->receiver);
              if (peer != NULL)
                {
                  wg_process_data_message(priv, peer, msg, len - 16, addr,
                                           port);
                }
            }
        }
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: wg_run_timers
 *
 * Description:
 *   Periodic per-peer maintenance: keep-alives, rekeying, and peer/session
 *   expiry. Equivalent to wireguardif_tmr() in upstream wireguardif.c,
 *   but driven from the RX thread's receive timeout instead of a separate
 *   lwIP sys_timeout().
 *
 ****************************************************************************/

static void wg_run_timers(FAR struct wg_netdev_s *priv)
{
  bool link_up = false;
  int x;

  net_lock();

  for (x = 0; x < WIREGUARD_MAX_PEERS; x++)
    {
      FAR struct wireguard_peer *peer = &priv->wg.peers[x];
      bool can_send_initiation;
      bool should_initiate;
      uint32_t rekey_at;

      if (!peer->valid)
        {
          continue;
        }

      if (peer->curr_keypair.valid &&
          wireguard_expired(peer->curr_keypair.keypair_millis,
                             REJECT_AFTER_TIME * 3))
        {
          keypair_destroy(&peer->next_keypair);
          keypair_destroy(&peer->curr_keypair);
          keypair_destroy(&peer->prev_keypair);

          peer->ip = peer->connect_ip;
          peer->port = peer->connect_port;
        }

      if (peer->curr_keypair.valid &&
          (wireguard_expired(peer->curr_keypair.keypair_millis,
                              REJECT_AFTER_TIME) ||
           peer->curr_keypair.sending_counter >= REJECT_AFTER_MESSAGES))
        {
          keypair_destroy(&peer->curr_keypair);
        }

      if (peer->keepalive_interval > 0 &&
          (peer->curr_keypair.valid || peer->prev_keypair.valid) &&
          wireguard_expired(peer->last_tx, peer->keepalive_interval))
        {
          wg_send_keepalive(priv, peer);
        }

      can_send_initiation = (peer->last_initiation_tx == 0) ||
                             wireguard_expired(peer->last_initiation_tx,
                                                REKEY_TIMEOUT);

      rekey_at = REJECT_AFTER_TIME - peer->keepalive_interval;

      should_initiate = can_send_initiation &&
          (peer->send_handshake ||
           (peer->curr_keypair.valid && !peer->curr_keypair.initiator &&
            wireguard_expired(peer->curr_keypair.keypair_millis,
                               rekey_at)) ||
           (!peer->curr_keypair.valid && peer->active));

      if (should_initiate)
        {
          wg_start_handshake(priv, peer);
        }

      if (peer->curr_keypair.valid || peer->prev_keypair.valid)
        {
          link_up = true;
        }
    }

  if (!link_up)
    {
      netdev_carrier_off(&priv->dev);
    }

  net_unlock();
}

/****************************************************************************
 * Name: wg_poll_cb
 *
 * Description:
 *   poll callback installed by wg_rx_task(). The network stack invokes it
 *   when the UDP socket becomes readable; all it does is wake the task.
 *
 ****************************************************************************/

static void wg_poll_cb(FAR struct pollfd *fds)
{
  FAR sem_t *sem = (FAR sem_t *)fds->arg;

  nxsem_post(sem);
}

/****************************************************************************
 * Name: wg_rx_task
 *
 * Description:
 *   Background task: blocks waiting for incoming WireGuard UDP traffic and
 *   doubles as the periodic timer, running wg_run_timers() on every wakeup
 *   whether that came from data arriving or from the WG_TIMER_MSECS
 *   timeout.
 *
 *   priv->psock is a raw struct socket (see its declaration), read and
 *   written with the psock_*() internal API instead of the usual fd-based
 *   socket()/recvfrom()/sendto()/poll(). This is required, not a style
 *   choice: a NuttX file descriptor is only valid within the task (and
 *   its descendants at task_create() time) that owned it when it was
 *   created. wg_txpoll() - the TX side of this same socket - runs on the
 *   system LPWORK thread (queued by wg_txavail(), itself called from
 *   whichever task is doing an outgoing send(), e.g. a telnetd session
 *   task), which is unrelated to wg_rx_task by any task_create() lineage.
 *   sendto() on an fd from there fails with EBADF; this was diagnosed by
 *   instrumenting the TX path and reproducing over a real WireGuard
 *   tunnel: the handshake and any reply built synchronously inside
 *   ipv4_input() (ICMP echo, SYN-ACK) went out fine because those run
 *   inside wg_rx_task's own call stack, but every send queued
 *   asynchronously through devif_poll()/wg_txavail() - i.e. all TCP
 *   application data - silently failed with EBADF from the LPWORK
 *   thread, while wg_show()'s counters made it look like nothing was
 *   even attempted. struct socket has no such restriction: it is a plain
 *   struct dereferenced by pointer, so psock_sendto() works identically
 *   from any task.
 *
 *   The wait itself is psock_poll() rather than poll(), for the same
 *   reason: poll() takes descriptors and so carries the same fd-table
 *   restriction. psock_poll() drives a caller-supplied struct pollfd
 *   directly, so this task installs wg_poll_cb() on it and blocks on a
 *   semaphore until either the socket becomes readable or the timeout
 *   expires. (SO_RCVTIMEO would have been the obvious alternative but is
 *   accepted-and-ignored on this platform - see git history.) There is no
 *   lost-wakeup window around the teardown/setup pair: udp_pollsetup()
 *   notifies immediately if the read-ahead queue is already non-empty.
 *
 *   This is a standalone task (task_create()), not a pthread of the "wg"
 *   NSH command: a pthread created and detached by "wg" was empirically
 *   found to stop running as soon as the "wg" command's task exited
 *   (confirmed by adding trace prints - none from the thread ever
 *   appeared once "wg" returned to the NSH prompt, even though the
 *   thread is detached and its loop has no exit condition). A task
 *   created with task_create() is independent of its creator's lifetime.
 *
 ****************************************************************************/

static int wg_rx_task(int argc, FAR char *argv[])
{
  FAR struct wg_netdev_s *priv = &g_wg;
  uint8_t rxbuf[WG_MAX_PACKET];
  struct pollfd fds;
  sem_t waitsem;

  nxsem_init(&waitsem, 0, 0);

  while (priv->running)
    {
      struct sockaddr_in from;
      socklen_t fromlen;
      ssize_t n;

      /* Block until the socket is readable or the timer interval expires */

      memset(&fds, 0, sizeof(fds));
      fds.events = POLLIN;
      fds.arg    = &waitsem;
      fds.cb     = wg_poll_cb;

      if (psock_poll(&priv->psock, &fds, true) >= 0)
        {
          nxsem_tickwait(&waitsem, MSEC2TICK(WG_TIMER_MSECS));
          psock_poll(&priv->psock, &fds, false);
        }

      /* Drop any extra posts the callback made while we were awake, so the
       * next iteration blocks properly instead of spinning through a
       * backlog of stale wakeups.
       */

      while (nxsem_trywait(&waitsem) >= 0);

      /* Drain everything queued, not just one datagram: a single wakeup can
       * cover several packets that arrived close together.
       */

      for (; ; )
        {
          fromlen = sizeof(from);
          n = psock_recvfrom(&priv->psock, rxbuf, sizeof(rxbuf),
                             MSG_DONTWAIT, (FAR struct sockaddr *)&from,
                             &fromlen);
          if (n <= 0)
            {
              break;
            }

          net_lock();
          wg_process_udp_packet(priv, rxbuf, (size_t)n,
                                from.sin_addr.s_addr,
                                ntohs(from.sin_port));
          net_unlock();
        }

      wg_run_timers(priv);
    }

  nxsem_destroy(&waitsem);
  return 0;
}

/****************************************************************************
 * Name: wg_txpoll
 *
 * Description:
 *   Callback from devif_poll(): the stack has a plaintext packet queued for
 *   wg0 in dev->d_iob/d_len. Encrypt it and send it to whichever peer's
 *   allowed-ips cover the destination address.
 *
 ****************************************************************************/

static int wg_txpoll(FAR struct net_driver_s *dev)
{
  FAR struct wg_netdev_s *priv = (FAR struct wg_netdev_s *)dev->d_private;
  size_t len = dev->d_len;

  if (len > 0 && len <= sizeof(priv->plainbuf) && dev->d_iob != NULL)
    {
      FAR struct ipv4_hdr_s *iphdr;
      in_addr_t dest;
      FAR struct wireguard_peer *peer;

      iob_copyout(priv->plainbuf, dev->d_iob, len, 0);

      iphdr = (FAR struct ipv4_hdr_s *)priv->plainbuf;
      dest = net_ip4addr_conv32(iphdr->destipaddr);
      peer = wg_peer_for_dest(priv, dest);
      if (peer != NULL)
        {
          wg_encrypt_and_send(priv, peer, priv->plainbuf, len);
        }
    }

  netdev_iob_release(dev);
  return 0;
}

static void wg_txavail_work(FAR void *arg)
{
  FAR struct wg_netdev_s *priv = (FAR struct wg_netdev_s *)arg;

  net_lock();
  if (priv->bifup)
    {
      devif_poll(&priv->dev, wg_txpoll);
    }

  net_unlock();
}

static int wg_txavail(FAR struct net_driver_s *dev)
{
  FAR struct wg_netdev_s *priv = (FAR struct wg_netdev_s *)dev->d_private;

  if (work_available(&priv->txwork))
    {
      work_queue(WG_TXWORK, &priv->txwork, wg_txavail_work, priv, 0);
    }

  return OK;
}

/****************************************************************************
 * Name: wg_ifup / wg_ifdown
 ****************************************************************************/

static int wg_ifup(FAR struct net_driver_s *dev)
{
  FAR struct wg_netdev_s *priv = (FAR struct wg_netdev_s *)dev->d_private;
  int x;

  priv->bifup = true;
  netdev_carrier_on(dev);

  /* Kick off connections to any peer that has a configured endpoint,
   * matching wireguardif_connect() being called for such peers.
   */

  for (x = 0; x < WIREGUARD_MAX_PEERS; x++)
    {
      FAR struct wireguard_peer *peer = &priv->wg.peers[x];

      if (peer->valid && peer->connect_ip.s_addr != 0 &&
          peer->connect_port > 0)
        {
          peer->active = true;
          peer->ip = peer->connect_ip;
          peer->port = peer->connect_port;
          peer->send_handshake = true;
        }
    }

  return OK;
}

static int wg_ifdown(FAR struct net_driver_s *dev)
{
  FAR struct wg_netdev_s *priv = (FAR struct wg_netdev_s *)dev->d_private;

  netdev_carrier_off(dev);
  priv->bifup = false;
  return OK;
}

/****************************************************************************
 * Name: wg_configure_peer
 *
 * Description:
 *   Set up the single Kconfig-provided peer, if a public key was given.
 *   This is a minimal stand-in for the "wg setconf" NSH command planned
 *   for a later phase.
 *
 ****************************************************************************/

static void wg_configure_peer(FAR struct wg_netdev_s *priv)
{
  uint8_t public_key[WIREGUARD_PUBLIC_KEY_LEN];
  size_t public_key_len = sizeof(public_key);
  FAR struct wireguard_peer *peer;
  struct in_addr addr;

  if (strlen(CONFIG_NET_WIREGUARD_PEER_PUBLIC_KEY) == 0)
    {
      return;
    }

  if (!wireguard_base64_decode(CONFIG_NET_WIREGUARD_PEER_PUBLIC_KEY,
                                public_key, &public_key_len) ||
      public_key_len != WIREGUARD_PUBLIC_KEY_LEN)
    {
      nerr("ERROR: CONFIG_NET_WIREGUARD_PEER_PUBLIC_KEY is not a valid "
           "base64 key\n");
      return;
    }

  peer = peer_alloc(&priv->wg);
  if (peer == NULL)
    {
      nerr("ERROR: no free WireGuard peer slots\n");
      return;
    }

  if (!wireguard_peer_init(&priv->wg, peer, public_key, NULL))
    {
      nerr("ERROR: wireguard_peer_init failed\n");
      return;
    }

  peer->keepalive_interval = CONFIG_NET_WIREGUARD_PEER_KEEPALIVE;

  if (inet_pton(AF_INET, CONFIG_NET_WIREGUARD_PEER_ALLOWED_IP, &addr) == 1)
    {
      peer->allowed_source_ips[0].ip = addr;
    }

  if (inet_pton(AF_INET, CONFIG_NET_WIREGUARD_PEER_ALLOWED_MASK, &addr) == 1)
    {
      peer->allowed_source_ips[0].mask = addr;
    }

  peer->allowed_source_ips[0].valid = true;

  if (strlen(CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP) > 0 &&
      inet_pton(AF_INET, CONFIG_NET_WIREGUARD_PEER_ENDPOINT_IP, &addr) == 1)
    {
      peer->connect_ip = addr;
      peer->connect_port = CONFIG_NET_WIREGUARD_PEER_ENDPOINT_PORT;
      peer->ip = peer->connect_ip;
      peer->port = peer->connect_port;
    }
}

/****************************************************************************
 * Name: wg_configure_address
 *
 * Description:
 *   Assign wg0's own tunnel address from Kconfig and bring the interface
 *   up (triggers wg_ifup() via the SIOCSIFFLAGS ioctl, same as running
 *   "ifconfig wg0 up" from NSH).
 *
 ****************************************************************************/

static void wg_configure_address(FAR struct wg_netdev_s *priv)
{
  struct in_addr addr;

  if (inet_pton(AF_INET, CONFIG_NET_WIREGUARD_LOCAL_IPADDR, &addr) == 1)
    {
      netlib_set_ipv4addr("wg0", &addr);
    }

  if (inet_pton(AF_INET, CONFIG_NET_WIREGUARD_LOCAL_NETMASK, &addr) == 1)
    {
      netlib_set_ipv4netmask("wg0", &addr);
    }

  netlib_ifup("wg0");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int wg_initialize(void)
{
  uint8_t private_key[WIREGUARD_PRIVATE_KEY_LEN];
  size_t private_key_len = sizeof(private_key);
  struct sockaddr_in bindaddr;
  FAR struct wg_netdev_s *priv = &g_wg;
  int ret;

  if (priv->registered)
    {
      return OK;
    }

  if (strlen(CONFIG_NET_WIREGUARD_PRIVATE_KEY) == 0)
    {
      nerr("ERROR: CONFIG_NET_WIREGUARD_PRIVATE_KEY is not set\n");
      return -EINVAL;
    }

  if (!wireguard_base64_decode(CONFIG_NET_WIREGUARD_PRIVATE_KEY, private_key,
                                &private_key_len) ||
      private_key_len != WIREGUARD_PRIVATE_KEY_LEN)
    {
      nerr("ERROR: CONFIG_NET_WIREGUARD_PRIVATE_KEY is not a valid base64 "
           "key\n");
      return -EINVAL;
    }

  memset(priv, 0, sizeof(*priv));

  wireguard_init();

  if (!wireguard_device_init(&priv->wg, private_key))
    {
      nerr("ERROR: wireguard_device_init failed\n");
      return -EINVAL;
    }

  ret = psock_socket(AF_INET, SOCK_DGRAM, 0, &priv->psock);
  if (ret < 0)
    {
      nerr("ERROR: psock_socket() failed: %d\n", ret);
      return ret;
    }

  memset(&bindaddr, 0, sizeof(bindaddr));
  bindaddr.sin_family = AF_INET;
  bindaddr.sin_addr.s_addr = INADDR_ANY;
  bindaddr.sin_port = htons(CONFIG_NET_WIREGUARD_LISTEN_PORT);

  ret = psock_bind(&priv->psock, (FAR struct sockaddr *)&bindaddr,
                    sizeof(bindaddr));
  if (ret < 0)
    {
      nerr("ERROR: psock_bind() failed: %d\n", ret);
      psock_close(&priv->psock);
      return ret;
    }

  wg_configure_peer(priv);

  priv->dev.d_ifup    = wg_ifup;
  priv->dev.d_ifdown  = wg_ifdown;
  priv->dev.d_txavail = wg_txavail;
  priv->dev.d_private = priv;
  strlcpy(priv->dev.d_ifname, "wg0", IFNAMSIZ);

  ret = netdev_register(&priv->dev, NET_LL_TUN);
  if (ret < 0)
    {
      nerr("ERROR: netdev_register failed: %d\n", ret);
      psock_close(&priv->psock);
      return ret;
    }

  priv->running = true;

  ret = task_create("wg_rx", CONFIG_NET_WIREGUARD_PRIORITY,
                     CONFIG_NET_WIREGUARD_RX_STACKSIZE, wg_rx_task, NULL);
  if (ret < 0)
    {
      ret = -errno;
      nerr("ERROR: task_create failed: %d\n", ret);
      netdev_unregister(&priv->dev);
      psock_close(&priv->psock);
      return ret;
    }

  priv->rxtask = ret;

  wg_configure_address(priv);

  priv->registered = true;
  return OK;
}

/****************************************************************************
 * Name: wg_show
 *
 * Description:
 *   Print interface and peer status to stdout, similar in spirit to the
 *   upstream "wg show" command. Backs the "wg show" NSH subcommand.
 ****************************************************************************/

void wg_show(void)
{
  FAR struct wg_netdev_s *priv = &g_wg;
  char keybuf[64];
  size_t keylen;
  int x;

  if (!priv->registered)
    {
      printf("wg0 is not up (run \"wg\" first)\n");
      return;
    }

  keylen = sizeof(keybuf) - 1;
  if (wireguard_base64_encode(priv->wg.public_key, WIREGUARD_PUBLIC_KEY_LEN,
                               keybuf, &keylen))
    {
      keybuf[keylen] = '\0';
    }
  else
    {
      strlcpy(keybuf, "(unknown)", sizeof(keybuf));
    }

  printf("interface: wg0\n");
  printf("  public key: %s\n", keybuf);
  printf("  listening port: %d\n", CONFIG_NET_WIREGUARD_LISTEN_PORT);

  for (x = 0; x < WIREGUARD_MAX_PEERS; x++)
    {
      FAR struct wireguard_peer *peer = &priv->wg.peers[x];
      FAR struct wireguard_keypair *active_keypair = NULL;
      char ipbuf[INET_ADDRSTRLEN];
      uint32_t now;

      if (!peer->valid)
        {
          continue;
        }

      keylen = sizeof(keybuf) - 1;
      if (wireguard_base64_encode(peer->public_key, WIREGUARD_PUBLIC_KEY_LEN,
                                   keybuf, &keylen))
        {
          keybuf[keylen] = '\0';
        }
      else
        {
          strlcpy(keybuf, "(unknown)", sizeof(keybuf));
        }

      printf("peer: %s\n", keybuf);

      if (peer->port > 0)
        {
          inet_ntop(AF_INET, &peer->ip, ipbuf, sizeof(ipbuf));
          printf("  endpoint: %s:%u\n", ipbuf, peer->port);
        }

      if (peer->curr_keypair.valid)
        {
          active_keypair = &peer->curr_keypair;
        }
      else if (peer->prev_keypair.valid)
        {
          active_keypair = &peer->prev_keypair;
        }

      now = wireguard_sys_now();
      if (active_keypair != NULL)
        {
          printf("  latest handshake: %u seconds ago\n",
                 (unsigned int)((now - active_keypair->keypair_millis) /
                                 1000));
        }
      else
        {
          printf("  latest handshake: (never)\n");
        }

      printf("  transfer: %llu B received, %llu B sent\n",
             (unsigned long long)priv->peer_rx_bytes[x],
             (unsigned long long)priv->peer_tx_bytes[x]);
    }
}
