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
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: wg_initialize
 *
 * Description:
 *   Register the "wg0" network interface, configure it (private key,
 *   listen port, local address, and optionally one peer) from Kconfig
 *   settings, and start the background thread that handles the UDP
 *   transport for the tunnel.
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
