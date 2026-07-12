/****************************************************************************
 * apps/netutils/wireguard/wg_main.c
 *
 * NSH command line front-end for the NuttX WireGuard interface.
 *
 *   wg genkey                        Generate a new private key
 *   wg pubkey <private-key>          Derive the public key
 *   wg up -k <key> -a <addr> [-m <mask>] [-p <port>]
 *                                    Bring up the wg0 interface
 *   wg peer -P <pubkey> -A <ip[/prefix]> [-e <ip:port>] [-K <seconds>]
 *                                    Add a peer (connects if -e given)
 *   wg status                        Show device and peer state
 *   wg down                          Tear down the interface
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "wireguard.h"
#include "crypto.h"
#include "nuttx-wireguardif.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void wg_usage(void)
{
  printf("WireGuard for NuttX\n");
  printf("Usage:\n");
  printf("  wg genkey                      generate a private key\n");
  printf("  wg pubkey <private-key>        derive public key\n");
  printf("  wg up -k <private-key> -a <addr> [-m <netmask>] "
         "[-p <listen-port>]\n");
  printf("  wg peer -P <public-key> -A <ip[/prefix]> [-e <ip:port>] "
         "[-K <keepalive-sec>]\n");
  printf("  wg status\n");
  printf("  wg down\n");
}

static int wg_parse_addr(const char *str, struct in_addr *addr)
{
  return inet_pton(AF_INET, str, addr) == 1 ? OK : -EINVAL;
}

/* Parse "a.b.c.d" or "a.b.c.d/prefix" */

static int wg_parse_cidr(const char *str, struct in_addr *addr,
                         struct in_addr *mask)
{
  char buf[32];
  char *slash;
  int prefix = 32;

  strlcpy(buf, str, sizeof(buf));
  slash = strchr(buf, '/');
  if (slash)
    {
      *slash = '\0';
      prefix = atoi(slash + 1);
      if (prefix < 0 || prefix > 32)
        {
          return -EINVAL;
        }
    }

  if (wg_parse_addr(buf, addr) != OK)
    {
      return -EINVAL;
    }

  if (prefix == 0)
    {
      mask->s_addr = 0;
    }
  else
    {
      mask->s_addr = htonl(0xffffffffu << (32 - prefix));
    }

  return OK;
}

/* Parse "a.b.c.d:port" */

static int wg_parse_endpoint(const char *str, struct in_addr *addr,
                             uint16_t *port)
{
  char buf[32];
  char *colon;
  int p;

  strlcpy(buf, str, sizeof(buf));
  colon = strchr(buf, ':');
  if (!colon)
    {
      return -EINVAL;
    }

  *colon = '\0';
  p = atoi(colon + 1);
  if (p <= 0 || p > 65535)
    {
      return -EINVAL;
    }

  if (wg_parse_addr(buf, addr) != OK)
    {
      return -EINVAL;
    }

  *port = (uint16_t)p;
  return OK;
}

static int wg_cmd_genkey(void)
{
  uint8_t key[WIREGUARD_PRIVATE_KEY_LEN];
  char b64[48];
  size_t outlen = sizeof(b64);

  wireguard_random_bytes(key, sizeof(key));

  /* Curve25519 clamp */

  key[0]  &= 248;
  key[31] &= 127;
  key[31] |= 64;

  if (!wireguard_base64_encode(key, sizeof(key), b64, &outlen))
    {
      crypto_zero(key, sizeof(key));
      return ERROR;
    }

  crypto_zero(key, sizeof(key));
  printf("%s\n", b64);
  return OK;
}

static int wg_cmd_pubkey(const char *private_b64)
{
  static const uint8_t basepoint[WIREGUARD_PUBLIC_KEY_LEN] =
  {
    9
  };

  uint8_t priv[WIREGUARD_PRIVATE_KEY_LEN];
  uint8_t pub[WIREGUARD_PUBLIC_KEY_LEN];
  size_t privlen = sizeof(priv);
  char b64[48];
  size_t outlen = sizeof(b64);

  if (!wireguard_base64_decode(private_b64, priv, &privlen) ||
      privlen != WIREGUARD_PRIVATE_KEY_LEN)
    {
      fprintf(stderr, "wg: invalid private key\n");
      return ERROR;
    }

  if (wireguard_x25519(pub, priv, basepoint) != 0)
    {
      crypto_zero(priv, sizeof(priv));
      fprintf(stderr, "wg: key derivation failed\n");
      return ERROR;
    }

  crypto_zero(priv, sizeof(priv));

  if (!wireguard_base64_encode(pub, sizeof(pub), b64, &outlen))
    {
      return ERROR;
    }

  printf("%s\n", b64);
  return OK;
}

static int wg_cmd_up(int argc, char **argv)
{
  const char *key = NULL;
  struct in_addr addr;
  struct in_addr mask;
  int port = WIREGUARDIF_DEFAULT_PORT;
  bool have_addr = false;
  int opt;
  int ret;

  addr.s_addr = 0;
  wg_parse_addr("255.255.255.0", &mask);

  optind = 1;
  while ((opt = getopt(argc, argv, "k:a:m:p:")) != -1)
    {
      switch (opt)
        {
          case 'k':
            key = optarg;
            break;

          case 'a':
            if (wg_parse_addr(optarg, &addr) != OK)
              {
                fprintf(stderr, "wg: bad address '%s'\n", optarg);
                return ERROR;
              }

            have_addr = true;
            break;

          case 'm':
            if (wg_parse_addr(optarg, &mask) != OK)
              {
                fprintf(stderr, "wg: bad netmask '%s'\n", optarg);
                return ERROR;
              }
            break;

          case 'p':
            port = atoi(optarg);
            break;

          default:
            wg_usage();
            return ERROR;
        }
    }

  if (!key || !have_addr || port <= 0 || port > 65535)
    {
      wg_usage();
      return ERROR;
    }

  ret = wireguardif_start(key, (uint16_t)port, addr, mask);
  if (ret < 0)
    {
      fprintf(stderr, "wg: start failed: %d\n", ret);
      return ERROR;
    }

  printf("%s: up, listening on udp/%d\n", wireguardif_ifname(), port);
  return OK;
}

static int wg_cmd_peer(int argc, char **argv)
{
  struct wireguardif_peer_config config;
  uint8_t preshared[WIREGUARD_SESSION_KEY_LEN];
  size_t preshared_len;
  bool have_allowed = false;
  bool have_endpoint = false;
  uint8_t peer_index = WIREGUARDIF_INVALID_INDEX;
  int opt;
  int ret;

  wireguardif_peer_config_init(&config);

  optind = 1;
  while ((opt = getopt(argc, argv, "P:A:e:K:s:")) != -1)
    {
      switch (opt)
        {
          case 'P':
            config.public_key = optarg;
            break;

          case 'A':
            if (wg_parse_cidr(optarg, &config.allowed_ip,
                              &config.allowed_mask) != OK)
              {
                fprintf(stderr, "wg: bad allowed-ips '%s'\n", optarg);
                return ERROR;
              }

            have_allowed = true;
            break;

          case 'e':
            if (wg_parse_endpoint(optarg, &config.endpoint_ip,
                                  &config.endpoint_port) != OK)
              {
                fprintf(stderr, "wg: bad endpoint '%s'\n", optarg);
                return ERROR;
              }

            have_endpoint = true;
            break;

          case 'K':
            config.keep_alive = (uint16_t)atoi(optarg);
            break;

          case 's':
            preshared_len = sizeof(preshared);
            if (!wireguard_base64_decode(optarg, preshared,
                                         &preshared_len) ||
                preshared_len != WIREGUARD_SESSION_KEY_LEN)
              {
                fprintf(stderr, "wg: bad preshared key\n");
                return ERROR;
              }

            config.preshared_key = preshared;
            break;

          default:
            wg_usage();
            return ERROR;
        }
    }

  if (!config.public_key || !have_allowed)
    {
      wg_usage();
      return ERROR;
    }

  ret = wireguardif_add_peer(&config, &peer_index);
  if (ret < 0 || peer_index == WIREGUARDIF_INVALID_INDEX)
    {
      fprintf(stderr, "wg: add peer failed: %d\n", ret);
      return ERROR;
    }

  printf("peer %u added\n", peer_index);

  if (have_endpoint)
    {
      ret = wireguardif_connect(peer_index);
      if (ret < 0)
        {
          fprintf(stderr, "wg: connect failed: %d\n", ret);
          return ERROR;
        }

      printf("peer %u: handshake initiated\n", peer_index);
    }

  return OK;
}

static int wg_cmd_status(void)
{
  struct wireguardif_peer_status status;
  char pubkey[48];
  char addrstr[INET_ADDRSTRLEN];
  uint32_t now;
  int i;

  if (!wireguardif_running())
    {
      printf("wireguard: not running\n");
      return OK;
    }

  printf("interface: %s\n", wireguardif_ifname());
  printf("  listening port: %u\n", wireguardif_listen_port());

  if (wireguardif_get_public_key(pubkey, sizeof(pubkey)) == OK)
    {
      printf("  public key: %s\n", pubkey);
    }

  now = wireguard_sys_now();

  for (i = 0; i < WIREGUARD_MAX_PEERS; i++)
    {
      if (wireguardif_peer_status((uint8_t)i, &status) != OK ||
          !status.valid)
        {
          continue;
        }

      printf("\npeer %d: %s\n", i, status.public_key);
      printf("  status: %s\n", status.up ? "up" : "down");

      inet_ntop(AF_INET, &status.endpoint_ip, addrstr, sizeof(addrstr));
      printf("  endpoint: %s:%u\n", addrstr, status.endpoint_port);

      if (status.last_rx != 0)
        {
          printf("  last rx: %lu seconds ago\n",
                 (unsigned long)((now - status.last_rx) / 1000));
        }

      if (status.last_tx != 0)
        {
          printf("  last tx: %lu seconds ago\n",
                 (unsigned long)((now - status.last_tx) / 1000));
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  if (argc < 2)
    {
      wg_usage();
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "genkey") == 0)
    {
      return wg_cmd_genkey() == OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
  else if (strcmp(argv[1], "pubkey") == 0)
    {
      if (argc < 3)
        {
          wg_usage();
          return EXIT_FAILURE;
        }

      return wg_cmd_pubkey(argv[2]) == OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
  else if (strcmp(argv[1], "up") == 0)
    {
      return wg_cmd_up(argc - 1, &argv[1]) == OK ?
             EXIT_SUCCESS : EXIT_FAILURE;
    }
  else if (strcmp(argv[1], "peer") == 0)
    {
      return wg_cmd_peer(argc - 1, &argv[1]) == OK ?
             EXIT_SUCCESS : EXIT_FAILURE;
    }
  else if (strcmp(argv[1], "status") == 0)
    {
      return wg_cmd_status() == OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
  else if (strcmp(argv[1], "down") == 0)
    {
      if (wireguardif_stop() < 0)
        {
          fprintf(stderr, "wg: not running\n");
          return EXIT_FAILURE;
        }

      printf("wireguard: stopped\n");
      return EXIT_SUCCESS;
    }

  wg_usage();
  return EXIT_FAILURE;
}
