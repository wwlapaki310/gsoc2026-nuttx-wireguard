/****************************************************************************
 * apps/netutils/wireguard/nuttx-platform.c
 *
 * Platform abstraction layer for WireGuard on NuttX.
 * Implements wireguard-platform.h using POSIX APIs available in NuttX.
 *
 * Phase 2: Real implementations replacing Phase 1 stubs.
 ****************************************************************************/

#include "wireguard-platform.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>

/****************************************************************************
 * wireguard_sys_now
 *
 * Returns monotonic time in milliseconds.
 * Used for handshake timeout tracking and keepalive scheduling.
 ****************************************************************************/

uint32_t wireguard_sys_now(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

/****************************************************************************
 * wireguard_random_bytes
 *
 * Fills buffer with cryptographically random bytes for key generation
 * and nonce derivation. Reads from /dev/urandom.
 *
 * Requires CONFIG_DEV_RANDOM=y in NuttX configuration.
 ****************************************************************************/

void wireguard_random_bytes(void *bytes, size_t size)
{
  int fd;
  ssize_t n;
  size_t total = 0;
  uint8_t *buf = (uint8_t *)bytes;

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_ERR, "wireguard: open /dev/urandom failed: %d\n", errno);
      memset(bytes, 0, size);
      return;
    }

  while (total < size)
    {
      n = read(fd, buf + total, size - total);
      if (n < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          syslog(LOG_ERR, "wireguard: read /dev/urandom failed: %d\n", errno);
          memset(buf + total, 0, size - total);
          break;
        }
      total += (size_t)n;
    }

  close(fd);
}

/****************************************************************************
 * wireguard_tai64n_now
 *
 * Fills 12-byte buffer with current time in TAI64N format:
 *   - 8 bytes: seconds since TAI epoch (UNIX + 0x400000000000000a)
 *   - 4 bytes: nanoseconds (big-endian)
 *
 * Used in handshake initiation messages for replay attack prevention.
 * Requires CONFIG_CLOCK_REALTIME=y (standard in NuttX).
 ****************************************************************************/

void wireguard_tai64n_now(uint8_t *output)
{
  struct timespec ts;
  uint64_t sec;
  uint32_t nsec;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
      memset(output, 0, 12);
      return;
    }

  /* TAI64N epoch offset: 2^62 + 10 (accounts for leap seconds at epoch) */

  sec  = (uint64_t)ts.tv_sec + UINT64_C(0x400000000000000a);
  nsec = (uint32_t)ts.tv_nsec;

  /* Store in big-endian (network) byte order */

  output[0]  = (uint8_t)(sec >> 56);
  output[1]  = (uint8_t)(sec >> 48);
  output[2]  = (uint8_t)(sec >> 40);
  output[3]  = (uint8_t)(sec >> 32);
  output[4]  = (uint8_t)(sec >> 24);
  output[5]  = (uint8_t)(sec >> 16);
  output[6]  = (uint8_t)(sec >>  8);
  output[7]  = (uint8_t)(sec);
  output[8]  = (uint8_t)(nsec >> 24);
  output[9]  = (uint8_t)(nsec >> 16);
  output[10] = (uint8_t)(nsec >>  8);
  output[11] = (uint8_t)(nsec);
}

/****************************************************************************
 * wireguard_is_under_load
 *
 * Returns true if the system should issue cookie challenges instead of
 * completing handshakes. Always false for embedded targets — resource
 * exhaustion protection is not needed at embedded scale.
 ****************************************************************************/

bool wireguard_is_under_load(void)
{
  return false;
}
