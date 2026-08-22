/****************************************************************************
 * apps/netutils/wireguard/wg_main.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include "nuttx-wireguardif.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wg_usage
 ****************************************************************************/

static void wg_usage(void)
{
  fprintf(stderr, "usage: wg [up|show]\n");
  fprintf(stderr, "  up    bring wg0 up using the Kconfig settings"
                  " (default if no argument is given)\n");
  fprintf(stderr, "  show  print the interface and peer status\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   NSH builtin entry point. "wg" (or "wg up") brings the wg0 interface up
 *   using the private key, peer and address configured through Kconfig;
 *   "wg show" prints the current status.
 *
 *   Runtime configuration ("wg set" / "wg setconf" in the upstream
 *   wg(8) tool) is not implemented yet - see docs/code-review-2026-08.md.
 *
 * Input Parameters:
 *   argc, argv - Standard NSH builtin arguments
 *
 * Returned Value:
 *   Zero on success, 1 on failure.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int ret;

  if (argc >= 2 && strcmp(argv[1], "show") == 0)
    {
      wg_show();
      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "up") != 0)
    {
      fprintf(stderr, "wg: unknown subcommand '%s'\n", argv[1]);
      wg_usage();
      return 1;
    }

  ret = wg_initialize();
  if (ret < 0)
    {
      fprintf(stderr, "wg: failed to bring up wg0: %d\n", ret);
      return 1;
    }

  printf("wg0 is up (listen port %d)\n", CONFIG_NET_WIREGUARD_LISTEN_PORT);
  return 0;
}
