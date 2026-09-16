/****************************************************************************
 * apps/system/denyinet/denyinet_main.c
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

/* denyinet on|off
 *
 * Flip the usrsock daemon's SIOCDENYINETSOCK switch. With "on", every
 * AF_INET socket() opened from then on is refused by the daemon (-ENOTSUP)
 * and falls back to the kernel network stack, and netdev ioctls are
 * answered -ENOTTY so they reach the real netdev by name. Sockets that
 * already exist keep going through the daemon.
 *
 * This is what makes a WireGuard tunnel usable end to end on a board whose
 * Wi-Fi is a usrsock driver (Spresense + GS2200M): bring wg0 up first, so
 * its UDP socket is offloaded to the Wi-Fi module, then "denyinet on", then
 * start telnetd / webserver so their listening TCP sockets live in the
 * kernel stack where packets decrypted by wg0 are delivered.
 */

#include <nuttx/config.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <nuttx/net/ioctl.h>

int main(int argc, FAR char *argv[])
{
  uint8_t mode;
  int fd;
  int ret;

  if (argc != 2 ||
      (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0))
    {
      fprintf(stderr, "Usage: %s on|off\n", argv[0]);
      return 1;
    }

  mode = strcmp(argv[1], "on") == 0 ? DENY_INET_SOCK_ENABLE
                                    : DENY_INET_SOCK_DISABLE;

  /* SOCK_CTRL is exempt from the deny, so it reaches the daemon in both
   * directions; fall back to SOCK_DGRAM for daemons that reject it.
   */

  fd = socket(AF_INET, SOCK_CTRL, 0);
  if (fd < 0)
    {
      fd = socket(AF_INET, SOCK_DGRAM, 0);
    }

  if (fd < 0)
    {
      fprintf(stderr, "%s: socket() failed: %d\n", argv[0], errno);
      return 1;
    }

  ret = ioctl(fd, SIOCDENYINETSOCK, (unsigned long)(uintptr_t)&mode);
  close(fd);

  if (ret < 0)
    {
      fprintf(stderr, "%s: SIOCDENYINETSOCK failed: %d "
              "(is the usrsock daemon running?)\n", argv[0], errno);
      return 1;
    }

  printf("AF_INET sockets now go to the %s stack\n",
         mode == DENY_INET_SOCK_ENABLE ? "kernel" : "usrsock");
  return 0;
}
