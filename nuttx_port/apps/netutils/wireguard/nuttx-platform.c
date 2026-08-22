/****************************************************************************
 * apps/netutils/wireguard/nuttx-platform.c
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

/* Platform abstraction layer for WireGuard on NuttX. Implements the four
 * functions declared in wireguard-platform.h, which is where the vendored
 * wireguard-lwip sources isolate all of their OS dependencies.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "wireguard-platform.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* TAI64 label of the Unix epoch, per https://cr.yp.to/libtai/tai64.html.
 * WireGuard only requires this to be a strictly increasing value that both
 * ends of the handshake agree on, not a leap-second-accurate TAI clock.
 */

#define WG_TAI64_UNIX_EPOCH  0x400000000000000aull

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wireguard_sys_now
 *
 * Description:
 *   Return a monotonic millisecond timestamp. Used throughout the protocol
 *   core for handshake and keep-alive expiry.
 *
 * Returned Value:
 *   Milliseconds since an unspecified fixed point, wrapping at 2^32.
 *
 ****************************************************************************/

uint32_t wireguard_sys_now(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/****************************************************************************
 * Name: wireguard_random_bytes
 *
 * Description:
 *   Fill a buffer with cryptographically random bytes, used for ephemeral
 *   key generation. Requires CONFIG_DEV_URANDOM.
 *
 *   If no entropy source is available the buffer is zeroed rather than left
 *   holding whatever the caller's stack or heap contained: handing out
 *   uninitialised memory as "random" key material would silently weaken
 *   every handshake built on it.
 *
 * Input Parameters:
 *   bytes - Buffer to fill
 *   size  - Number of bytes to write
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void wireguard_random_bytes(FAR void *bytes, size_t size)
{
  FAR uint8_t *dst = (FAR uint8_t *)bytes;
  size_t total = 0;
  ssize_t nread;
  int fd;

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      memset(bytes, 0, size);
      return;
    }

  while (total < size)
    {
      nread = read(fd, dst + total, size - total);
      if (nread <= 0)
        {
          memset(dst + total, 0, size - total);
          break;
        }

      total += (size_t)nread;
    }

  close(fd);
}

/****************************************************************************
 * Name: wireguard_tai64n_now
 *
 * Description:
 *   Write the current time in TAI64N format (8 byte seconds followed by
 *   4 byte nanoseconds, both big endian). The protocol core embeds this in
 *   handshake initiation messages so that a peer can reject replayed
 *   handshakes.
 *
 * Input Parameters:
 *   output - 12 byte buffer to write the timestamp into
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void wireguard_tai64n_now(FAR uint8_t *output)
{
  struct timespec ts;
  uint64_t seconds;
  uint32_t nanos;
  int i;

  clock_gettime(CLOCK_REALTIME, &ts);

  seconds = (uint64_t)ts.tv_sec + WG_TAI64_UNIX_EPOCH;
  nanos = (uint32_t)ts.tv_nsec;

  for (i = 0; i < 8; i++)
    {
      output[i] = (uint8_t)(seconds >> (8 * (7 - i)));
    }

  for (i = 0; i < 4; i++)
    {
      output[8 + i] = (uint8_t)(nanos >> (8 * (3 - i)));
    }
}

/****************************************************************************
 * Name: wireguard_is_under_load
 *
 * Description:
 *   Report whether the device should answer handshake initiations with
 *   cookie replies instead of processing them, as a DoS mitigation.
 *
 *   Always false here: the cookie mechanism exists to protect servers
 *   fielding large volumes of handshakes, which is not the shape of a
 *   single-peer embedded device. See the note in wg_process_udp_packet()
 *   about the mac2/cookie path this makes unreachable.
 *
 * Returned Value:
 *   Always false.
 *
 ****************************************************************************/

bool wireguard_is_under_load(void)
{
  return false;
}
