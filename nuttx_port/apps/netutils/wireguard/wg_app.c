/****************************************************************************
 * apps/netutils/wireguard/wg_app.c
 *
 * NSH "wg" command for WireGuard configuration and status.
 *
 * Usage:
 *   wg init <private_key_hex> [<addr>/<prefix>] [port <port>]
 *   wg addpeer <pubkey_hex> endpoint <ip> <port> allowedips <cidr>
 *   wg connect <peer_idx>
 *   wg status
 *   wg down
 *
 * Example (inside NSH):
 *   wg init a8b4c3... 10.0.1.1/24
 *   wg addpeer f7e2d1... endpoint 192.168.1.100 51820 allowedips 10.0.1.2/32
 *   wg connect 0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "nuttx-wireguardif.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * parse_cidr - Parse "a.b.c.d/prefix" into addr + netmask.
 */

static int parse_cidr(const char *cidr, struct in_addr *addr,
                       struct in_addr *mask)
{
  char buf[32];
  char *slash;
  int prefix;
  uint32_t m;

  strncpy(buf, cidr, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  slash = strchr(buf, '/');
  if (slash)
    {
      *slash = '\0';
      prefix = atoi(slash + 1);
    }
  else
    {
      prefix = 32;
    }

  if (inet_aton(buf, addr) == 0)
    {
      return -EINVAL;
    }

  if (prefix < 0 || prefix > 32)
    {
      return -EINVAL;
    }

  m = prefix ? (~0u << (32 - prefix)) : 0;
  mask->s_addr = htonl(m);
  return 0;
}

/**
 * cmd_wg_init - "wg init <privkey_hex> [addr/prefix] [port <n>]"
 */

static int cmd_wg_init(int argc, char *argv[])
{
  struct wg_device_config cfg;
  int ret;

  if (argc < 2)
    {
      fprintf(stderr, "usage: wg init <private_key_hex> [<addr>/<prefix>] "
                      "[port <port>]\n");
      return -EINVAL;
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.listen_port = WG_DEFAULT_PORT;
  inet_aton("10.0.1.1", &cfg.address);
  inet_aton("255.255.255.0", &cfg.netmask);

  ret = wg_key_from_hex(cfg.private_key, argv[1]);
  if (ret < 0)
    {
      /* Try base64 */

      ret = wg_key_from_base64(cfg.private_key, argv[1]);
      if (ret < 0)
        {
          fprintf(stderr, "wg: invalid private key (must be 64-char hex or "
                          "44-char base64)\n");
          return ret;
        }
    }

  /* Parse optional addr/prefix and port */

  int i;
  for (i = 2; i < argc; i++)
    {
      if (strcmp(argv[i], "port") == 0 && i + 1 < argc)
        {
          cfg.listen_port = (uint16_t)atoi(argv[++i]);
        }
      else if (strchr(argv[i], '.') && strchr(argv[i], '/'))
        {
          struct in_addr mask;
          if (parse_cidr(argv[i], &cfg.address, &mask) == 0)
            {
              cfg.netmask = mask;
            }
        }
    }

  ret = wg_netdev_init(&cfg);
  if (ret < 0)
    {
      fprintf(stderr, "wg: init failed: %d\n", ret);
      return ret;
    }

  printf("wg0: up, addr %s, port %u\n",
         inet_ntoa(cfg.address), cfg.listen_port);
  return 0;
}

/**
 * cmd_wg_addpeer - "wg addpeer <pubkey> endpoint <ip> <port>
 *                              allowedips <cidr> [psk <pskkey>]
 *                              [keepalive <sec>]"
 */

static int cmd_wg_addpeer(int argc, char *argv[])
{
  struct wg_peer_config peer;
  uint8_t peer_idx;
  int ret;
  int i;

  if (argc < 6)
    {
      fprintf(stderr, "usage: wg addpeer <pubkey_hex> endpoint <ip> <port> "
                      "allowedips <cidr>\n");
      return -EINVAL;
    }

  memset(&peer, 0, sizeof(peer));

  ret = wg_key_from_hex(peer.public_key, argv[1]);
  if (ret < 0)
    {
      ret = wg_key_from_base64(peer.public_key, argv[1]);
      if (ret < 0)
        {
          fprintf(stderr, "wg: invalid public key\n");
          return ret;
        }
    }

  for (i = 2; i < argc; i++)
    {
      if (strcmp(argv[i], "endpoint") == 0 && i + 2 < argc)
        {
          if (inet_aton(argv[i + 1], &peer.endpoint_ip) == 0)
            {
              fprintf(stderr, "wg: invalid endpoint IP\n");
              return -EINVAL;
            }
          peer.endpoint_port = (uint16_t)atoi(argv[i + 2]);
          i += 2;
        }
      else if (strcmp(argv[i], "allowedips") == 0 && i + 1 < argc)
        {
          struct in_addr mask;
          if (parse_cidr(argv[++i], &peer.allowed_ip, &mask) < 0)
            {
              fprintf(stderr, "wg: invalid allowedips CIDR\n");
              return -EINVAL;
            }
          peer.allowed_mask = mask;
        }
      else if (strcmp(argv[i], "psk") == 0 && i + 1 < argc)
        {
          ret = wg_key_from_hex(peer.preshared_key, argv[++i]);
          if (ret < 0)
            {
              ret = wg_key_from_base64(peer.preshared_key, argv[i]);
              if (ret < 0)
                {
                  fprintf(stderr, "wg: invalid preshared key\n");
                  return ret;
                }
            }
        }
      else if (strcmp(argv[i], "keepalive") == 0 && i + 1 < argc)
        {
          peer.keepalive_interval = (uint16_t)atoi(argv[++i]);
        }
    }

  ret = wg_netdev_add_peer(&peer, &peer_idx);
  if (ret < 0)
    {
      fprintf(stderr, "wg: add_peer failed: %d\n", ret);
      return ret;
    }

  printf("wg0: peer %u added, endpoint %s:%u\n",
         peer_idx, inet_ntoa(peer.endpoint_ip), peer.endpoint_port);
  return 0;
}

/**
 * cmd_wg_connect - "wg connect <peer_idx>"
 */

static int cmd_wg_connect(int argc, char *argv[])
{
  int ret;

  if (argc < 2)
    {
      fprintf(stderr, "usage: wg connect <peer_idx>\n");
      return -EINVAL;
    }

  ret = wg_netdev_connect((uint8_t)atoi(argv[1]));
  if (ret < 0)
    {
      fprintf(stderr, "wg: connect peer %s failed: %d\n", argv[1], ret);
      return ret;
    }

  printf("wg0: handshake initiated with peer %s\n", argv[1]);
  return 0;
}

/**
 * cmd_wg_down - "wg down"
 */

static int cmd_wg_down(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  wg_netdev_shutdown();
  printf("wg0: down\n");
  return 0;
}

/**
 * cmd_wg_status - "wg status" — show brief interface info
 */

static int cmd_wg_status(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  /* TODO Phase 3: print key fingerprint, peer stats, session state */
  printf("wg0: interface is up (run 'ifconfig' for IP info)\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * wg_main - Entry point for the "wg" NSH command.
 */

int wg_main(int argc, char *argv[])
{
  if (argc < 2)
    {
      printf("usage: wg <command> [args]\n"
             "  init     <privkey> [<addr>/<prefix>] [port <port>]\n"
             "  addpeer  <pubkey> endpoint <ip> <port> allowedips <cidr>\n"
             "           [psk <key>] [keepalive <sec>]\n"
             "  connect  <peer_idx>\n"
             "  status\n"
             "  down\n");
      return 0;
    }

  if (strcmp(argv[1], "init") == 0)
    {
      return cmd_wg_init(argc - 1, argv + 1);
    }
  else if (strcmp(argv[1], "addpeer") == 0)
    {
      return cmd_wg_addpeer(argc - 1, argv + 1);
    }
  else if (strcmp(argv[1], "connect") == 0)
    {
      return cmd_wg_connect(argc - 1, argv + 1);
    }
  else if (strcmp(argv[1], "down") == 0)
    {
      return cmd_wg_down(argc - 1, argv + 1);
    }
  else if (strcmp(argv[1], "status") == 0)
    {
      return cmd_wg_status(argc - 1, argv + 1);
    }
  else
    {
      fprintf(stderr, "wg: unknown command '%s'\n", argv[1]);
      return -EINVAL;
    }
}
