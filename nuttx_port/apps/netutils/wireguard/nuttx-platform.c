/****************************************************************************
 * apps/netutils/wireguard/nuttx-platform.c
 *
 * Platform abstraction layer for WireGuard on NuttX.
 * Implements wireguard-platform.h for the NuttX RTOS.
 ****************************************************************************/

#include "wireguard-platform.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/****************************************************************************
 * wireguard_sys_now
 *
 * Returns monotonic time in milliseconds.  Wrap-around is fine: the
 * protocol code only ever computes (now - then) on uint32_t.
 ****************************************************************************/

uint32_t wireguard_sys_now(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000 +
                    (uint64_t)ts.tv_nsec / 1000000);
}

/****************************************************************************
 * wireguard_random_bytes
 *
 * Fills buffer with cryptographically random bytes for key generation.
 * Reads /dev/urandom (CONFIG_DEV_URANDOM) with /dev/random as fallback.
 *
 * Note: the descriptor must NOT be cached in a global.  This function is
 * called from different tasks (the transient wg command and the daemon)
 * and NuttX closes a task's descriptors when it exits, which would leave
 * a stale fd number behind and silently return garbage.
 ****************************************************************************/

void wireguard_random_bytes(void *bytes, size_t size)
{
  uint8_t *out = bytes;
  int fd;

  memset(bytes, 0, size);

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      fd = open("/dev/random", O_RDONLY);
      if (fd < 0)
        {
          return;
        }
    }

  while (size > 0)
    {
      ssize_t n = read(fd, out, size);
      if (n <= 0)
        {
          /* Should never happen with the random char devices; bail out
           * rather than spin forever.
           */

          break;
        }

      out  += n;
      size -= (size_t)n;
    }

  close(fd);
}

/****************************************************************************
 * wireguard_tai64n_now
 *
 * Returns current time in TAI64N format (8-byte seconds + 4-byte
 * nanoseconds, both big-endian) for handshake replay prevention.
 ****************************************************************************/

void wireguard_tai64n_now(uint8_t *output)
{
  struct timespec ts;
  uint64_t sec;
  uint32_t nsec;
  int i;

  clock_gettime(CLOCK_REALTIME, &ts);

  /* TAI64 labels for dates after 1970 are 2^62 + (TAI seconds).
   * 0x400000000000000a includes the 10 leap seconds offset at epoch.
   */

  sec  = 0x400000000000000aULL + (uint64_t)ts.tv_sec;
  nsec = (uint32_t)ts.tv_nsec;

  for (i = 0; i < 8; i++)
    {
      output[i] = (uint8_t)(sec >> (56 - 8 * i));
    }

  for (i = 0; i < 4; i++)
    {
      output[8 + i] = (uint8_t)(nsec >> (24 - 8 * i));
    }
}

/****************************************************************************
 * wireguard_is_under_load
 *
 * Returns true if the system should send cookie replies instead of
 * processing handshake initiations.  Always false for embedded targets.
 ****************************************************************************/

bool wireguard_is_under_load(void)
{
  return false;
}
