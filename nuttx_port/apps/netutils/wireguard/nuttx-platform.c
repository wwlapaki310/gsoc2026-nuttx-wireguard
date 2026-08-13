/****************************************************************************
 * apps/netutils/wireguard/nuttx-platform.c
 *
 * Platform abstraction layer for WireGuard on NuttX.
 * Implements wireguard-platform.h for the NuttX RTOS.
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
 * wireguard_sys_now
 *
 * Returns monotonic time in milliseconds.
 ****************************************************************************/

uint32_t wireguard_sys_now(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/****************************************************************************
 * wireguard_random_bytes
 *
 * Fills buffer with cryptographically random bytes for key generation.
 * Requires /dev/urandom (CONFIG_DEV_URANDOM=y).
 ****************************************************************************/

void wireguard_random_bytes(void *bytes, size_t size)
{
  int fd;
  ssize_t nread;
  size_t total = 0;
  uint8_t *dst = (uint8_t *)bytes;

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      /* No entropy source available - zero the buffer rather than handing
       * out uninitialised stack/heap memory as "random" key material.
       */

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
 * wireguard_tai64n_now
 *
 * Returns current time in TAI64N format (8-byte seconds + 4-byte nanoseconds)
 * for handshake replay attack prevention.
 ****************************************************************************/

void wireguard_tai64n_now(uint8_t *output)
{
  struct timespec ts;
  uint64_t seconds;
  uint32_t nanos;
  int i;

  clock_gettime(CLOCK_REALTIME, &ts);

  /* TAI64 label = Unix seconds + 2^62, per https://cr.yp.to/libtai/tai64.html.
   * WireGuard only needs this to be a strictly increasing value shared by
   * the algorithm's replay check, not a real leap-second-aware TAI clock.
   */

  seconds = (uint64_t)ts.tv_sec + 0x400000000000000AULL;
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
 * wireguard_is_under_load
 *
 * Returns true if the system should send cookie replies instead of
 * processing handshake initiations. Always false for embedded targets.
 ****************************************************************************/

bool wireguard_is_under_load(void)
{
  return false;
}
