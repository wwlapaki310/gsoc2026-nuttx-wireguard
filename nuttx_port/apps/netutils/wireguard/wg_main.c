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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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
  fprintf(stderr,
    "usage: wg [up|down|show|genkey|pubkey|set ...]\n"
    "\n"
    "  up                      bring wg0 up (default if no argument)\n"
    "  down                    take wg0 down\n"
    "  show                    print interface and peer status\n"
    "  showconf                print the configuration in wg(8) format\n"
    "  setconf <file>          load a wg(8) format configuration file\n"
    "  genkey                  print a new private key\n"
    "  pubkey <private-key>    print the matching public key\n"
    "\n"
    "  set private-key <key>\n"
    "  set peer <public-key> [endpoint <addr:port>]\n"
    "                         [allowed-ips <addr>/<prefix>]\n"
    "                         [persistent-keepalive <seconds>]\n"
    "  set peer <public-key> remove\n"
    "\n"
    "Naming an existing peer updates it; a new key adds one, up to\n"
    "CONFIG_NET_WIREGUARD_MAX_PEERS.\n"
    "\n"
    "\"set\" and \"setconf\" only work while wg0 is down, and override the\n"
    "matching CONFIG_NET_WIREGUARD_* build-time settings.\n"
    "\n"
    "To make a configuration survive a reboot, write it to the file the\n"
    "start-up script reads:\n"
    "  wg showconf > " CONFIG_NET_WIREGUARD_CONFIG_PATH "\n");
}

/****************************************************************************
 * Name: wg_do_set
 *
 * Description:
 *   Handle "wg set ...". argv is the full command line; argv[2] onward are
 *   the arguments to "set".
 *
 * Returned Value:
 *   Zero on success, 1 on failure.
 *
 ****************************************************************************/

static int wg_do_set(int argc, FAR char *argv[])
{
  FAR const char *endpoint = NULL;
  FAR const char *allowed = NULL;
  int keepalive = -1;
  int ret;
  int i;

  if (argc < 4)
    {
      wg_usage();
      return 1;
    }

  if (strcmp(argv[2], "private-key") == 0)
    {
      ret = wg_set_private_key(argv[3]);
      if (ret == -EBUSY)
        {
          fprintf(stderr, "wg: wg0 is up; run \"wg down\" first\n");
          return 1;
        }
      else if (ret < 0)
        {
          fprintf(stderr, "wg: not a valid base64 key\n");
          return 1;
        }

      return 0;
    }

  if (strcmp(argv[2], "peer") != 0)
    {
      fprintf(stderr, "wg: unknown setting '%s'\n", argv[2]);
      wg_usage();
      return 1;
    }

  /* "wg set peer <public-key> remove" */

  if (argc == 5 && strcmp(argv[4], "remove") == 0)
    {
      ret = wg_remove_peer(argv[3]);
      if (ret == -EBUSY)
        {
          fprintf(stderr, "wg: wg0 is up; run \"wg down\" first\n");
          return 1;
        }
      else if (ret == -ENOENT)
        {
          fprintf(stderr, "wg: no such peer\n");
          return 1;
        }

      return 0;
    }

  /* "wg set peer <public-key> [key value]... " */

  for (i = 4; i + 1 < argc; i += 2)
    {
      if (strcmp(argv[i], "endpoint") == 0)
        {
          endpoint = argv[i + 1];
        }
      else if (strcmp(argv[i], "allowed-ips") == 0)
        {
          allowed = argv[i + 1];
        }
      else if (strcmp(argv[i], "persistent-keepalive") == 0)
        {
          keepalive = atoi(argv[i + 1]);
        }
      else
        {
          fprintf(stderr, "wg: unknown peer setting '%s'\n", argv[i]);
          return 1;
        }
    }

  if (i != argc)
    {
      fprintf(stderr, "wg: '%s' is missing a value\n", argv[i]);
      return 1;
    }

  ret = wg_set_peer(argv[3], endpoint, allowed, keepalive);
  if (ret == -EBUSY)
    {
      fprintf(stderr, "wg: wg0 is up; run \"wg down\" first\n");
      return 1;
    }
  else if (ret < 0)
    {
      fprintf(stderr, "wg: could not parse the peer settings\n");
      return 1;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   NSH builtin entry point for the "wg" command. Loosely follows the
 *   upstream wg(8) command line, restricted to what a single-peer embedded
 *   interface needs.
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
  char key[WG_KEY_STRLEN];
  int ret;

  if (argc >= 2 && strcmp(argv[1], "show") == 0)
    {
      wg_show();
      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "genkey") == 0)
    {
      if (wg_genkey(key, sizeof(key)) < 0)
        {
          fprintf(stderr, "wg: could not generate a key\n");
          return 1;
        }

      printf("%s\n", key);
      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "pubkey") == 0)
    {
      if (argc < 3)
        {
          fprintf(stderr, "wg: pubkey needs a private key\n");
          return 1;
        }

      if (wg_pubkey(argv[2], key, sizeof(key)) < 0)
        {
          fprintf(stderr, "wg: not a valid base64 key\n");
          return 1;
        }

      printf("%s\n", key);
      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "showconf") == 0)
    {
      if (wg_showconf() < 0)
        {
          fprintf(stderr, "wg: no private key configured\n");
          return 1;
        }

      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "setconf") == 0)
    {
      FAR const char *path = argc >= 3 ? argv[2] :
                             CONFIG_NET_WIREGUARD_CONFIG_PATH;

      ret = wg_setconf(path);
      if (ret == -EBUSY)
        {
          fprintf(stderr, "wg: wg0 is up; run \"wg down\" first\n");
          return 1;
        }
      else if (ret < 0)
        {
          fprintf(stderr, "wg: could not load '%s': %d\n", path, ret);
          return 1;
        }

      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "set") == 0)
    {
      return wg_do_set(argc, argv);
    }

  if (argc >= 2 && strcmp(argv[1], "down") == 0)
    {
      ret = wg_down();
      if (ret == -ENODEV)
        {
          fprintf(stderr, "wg: wg0 is not up\n");
          return 1;
        }
      else if (ret < 0)
        {
          fprintf(stderr, "wg: failed to take wg0 down: %d\n", ret);
          return 1;
        }

      printf("wg0 is down\n");
      return 0;
    }

  if (argc >= 2 && strcmp(argv[1], "up") != 0)
    {
      fprintf(stderr, "wg: unknown subcommand '%s'\n", argv[1]);
      wg_usage();
      return 1;
    }

  if (wg_is_up())
    {
      printf("wg0 is already up\n");
      return 0;
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
