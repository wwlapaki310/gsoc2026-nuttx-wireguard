/****************************************************************************
 * apps/netutils/wireguard/nuttx-wireguardif.h
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

#ifndef __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H
#define __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Buffer size that comfortably holds a base64 encoded 32 byte key (44
 * characters) plus its NUL terminator.
 */

#define WG_KEY_STRLEN 48

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: wg_initialize
 *
 * Description:
 *   Register the "wg0" network interface, configure it (private key,
 *   listen port, local address, and optionally one peer), and start the
 *   background task that handles the UDP transport for the tunnel.
 *
 *   Settings staged with wg_set_private_key() / wg_set_peer() take
 *   precedence; anything not staged falls back to its Kconfig value, so a
 *   build that configures everything through Kconfig keeps working
 *   unchanged.
 *
 *   Safe to call more than once; subsequent calls are a no-op once the
 *   interface is up.
 *
 * Returned Value:
 *   0 (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int wg_initialize(void);

/****************************************************************************
 * Name: wg_down
 *
 * Description:
 *   Take wg0 down: stop the background task, unregister the network
 *   interface and close the UDP socket. After this returns, the staged
 *   configuration can be changed and wg_initialize() called again.
 *
 * Returned Value:
 *   0 (OK) on success; -ENODEV if wg0 was not up; -ETIMEDOUT if the
 *   background task did not stop.
 *
 ****************************************************************************/

int wg_down(void);

/****************************************************************************
 * Name: wg_is_up
 *
 * Description:
 *   True while wg0 is registered and its background task is running.
 *
 ****************************************************************************/

bool wg_is_up(void);

/****************************************************************************
 * Name: wg_set_private_key
 *
 * Description:
 *   Stage the interface private key, overriding
 *   CONFIG_NET_WIREGUARD_PRIVATE_KEY for the next wg_initialize().
 *
 * Input Parameters:
 *   b64 - Base64 encoded 32 byte Curve25519 private key.
 *
 * Returned Value:
 *   0 (OK) on success; -EINVAL if the key is not valid base64 of the right
 *   length; -EBUSY if wg0 is currently up.
 *
 ****************************************************************************/

int wg_set_private_key(FAR const char *b64);

/****************************************************************************
 * Name: wg_set_peer
 *
 * Description:
 *   Stage the peer configuration, overriding the CONFIG_NET_WIREGUARD_PEER_*
 *   settings for the next wg_initialize(). Only the public key is required;
 *   pass NULL for any part that should keep its Kconfig value.
 *
 * Input Parameters:
 *   pubkey_b64 - Base64 encoded 32 byte Curve25519 public key of the peer.
 *   endpoint   - "address:port", or NULL. When given, wg0 initiates the
 *                handshake to it; otherwise wg0 only answers handshakes the
 *                peer starts.
 *   allowed    - "address/prefix" (e.g. "10.10.0.1/32"), or NULL.
 *   keepalive  - Persistent keepalive in seconds, or -1 to leave unchanged.
 *
 * Returned Value:
 *   0 (OK) on success; -EINVAL if an argument could not be parsed;
 *   -EBUSY if wg0 is currently up.
 *
 ****************************************************************************/

int wg_set_peer(FAR const char *pubkey_b64, FAR const char *endpoint,
                FAR const char *allowed, int keepalive);

/****************************************************************************
 * Name: wg_genkey
 *
 * Description:
 *   Generate a new Curve25519 private key from the platform entropy source
 *   and return it base64 encoded. The key is clamped exactly as
 *   wireguard_device_init() would clamp it, so the value printed here is
 *   the value that will be used.
 *
 * Input Parameters:
 *   out    - Buffer receiving the NUL terminated base64 key.
 *   outlen - Size of out; must be at least WG_KEY_STRLEN.
 *
 * Returned Value:
 *   0 (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int wg_genkey(FAR char *out, size_t outlen);

/****************************************************************************
 * Name: wg_pubkey
 *
 * Description:
 *   Derive the public key matching a base64 encoded private key.
 *
 * Input Parameters:
 *   priv_b64 - Base64 encoded 32 byte Curve25519 private key.
 *   out      - Buffer receiving the NUL terminated base64 public key.
 *   outlen   - Size of out; must be at least WG_KEY_STRLEN.
 *
 * Returned Value:
 *   0 (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int wg_pubkey(FAR const char *priv_b64, FAR char *out, size_t outlen);

/****************************************************************************
 * Name: wg_showconf
 *
 * Description:
 *   Print the staged/active configuration to stdout in the same INI-style
 *   format wg(8) uses ([Interface] / [Peer] with PrivateKey, ListenPort,
 *   PublicKey, AllowedIPs, Endpoint, PersistentKeepalive). Redirecting this
 *   to a file produces something wg_setconf() can read back, and something
 *   a desktop WireGuard client would also accept.
 *
 *   Note this prints the private key in the clear, exactly as
 *   "wg showconf" does.
 *
 * Returned Value:
 *   0 (OK) on success; -ENODATA if no private key is configured.
 *
 ****************************************************************************/

int wg_showconf(void);

/****************************************************************************
 * Name: wg_setconf
 *
 * Description:
 *   Read a wg(8)-style configuration file and stage it, as though the
 *   equivalent wg_set_private_key() / wg_set_peer() calls had been made.
 *   Unrecognised keys are ignored so that files carrying wg-quick-only
 *   directives (Address, DNS, MTU ...) still load.
 *
 * Input Parameters:
 *   path - Configuration file to read.
 *
 * Returned Value:
 *   0 (OK) on success; -EBUSY if wg0 is up; a negated errno value if the
 *   file cannot be read or contains an unusable key.
 *
 ****************************************************************************/

int wg_setconf(FAR const char *path);

/****************************************************************************
 * Name: wg_show
 *
 * Description:
 *   Print wg0's interface and peer status to stdout (public key, listening
 *   port, and per-peer endpoint / latest handshake / transfer counters).
 *   Backs the "wg show" NSH subcommand.
 *
 ****************************************************************************/

void wg_show(void);

#endif /* __APPS_NETUTILS_WIREGUARD_NUTTX_WIREGUARDIF_H */
