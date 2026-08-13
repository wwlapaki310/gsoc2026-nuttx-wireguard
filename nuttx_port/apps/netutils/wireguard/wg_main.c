/****************************************************************************
 * apps/netutils/wireguard/wg_main.c
 *
 * NSH builtin entry point: "wg" brings up the wg0 interface using the
 * private key / peer / address configured via Kconfig.
 *
 * Phase 2 scope only: no subcommands yet. "wg show" / "wg setconf" are
 * planned for a later phase (see docs/proposal.ja.md).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include "nuttx-wireguardif.h"

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
      fprintf(stderr, "usage: wg [up|show]\n");
      fprintf(stderr, "  (no argument is the same as \"up\")\n");
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
