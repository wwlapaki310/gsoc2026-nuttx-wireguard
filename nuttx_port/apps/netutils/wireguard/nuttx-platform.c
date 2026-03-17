/****************************************************************************
 * apps/netutils/wireguard/nuttx-platform.c
 *
 * Platform abstraction layer for WireGuard on NuttX.
 * Implements wireguard-platform.h for the NuttX RTOS.
 *
 * Phase 1: Stub implementations (compiles, no functionality yet)
 * Phase 2: Replace stubs with real NuttX API calls
 ****************************************************************************/

#include "wireguard-platform.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/****************************************************************************
 * wireguard_sys_now
 *
 * Returns monotonic time in milliseconds.
 * Phase 2: clock_gettime(CLOCK_MONOTONIC)
 ****************************************************************************/

uint32_t wireguard_sys_now(void)
{
  return 0; /* TODO Phase 2: implement with clock_gettime(CLOCK_MONOTONIC) */
}

/****************************************************************************
 * wireguard_random_bytes
 *
 * Fills buffer with cryptographically random bytes for key generation.
 * Phase 2: read from /dev/urandom (requires CONFIG_DEV_RANDOM=y)
 ****************************************************************************/

void wireguard_random_bytes(void *bytes, size_t size)
{
  /* TODO Phase 2: implement with /dev/urandom */
  memset(bytes, 0, size);
}

/****************************************************************************
 * wireguard_tai64n_now
 *
 * Returns current time in TAI64N format (8-byte seconds + 4-byte nanoseconds)
 * for handshake replay attack prevention.
 * Phase 2: clock_gettime(CLOCK_REALTIME)
 ****************************************************************************/

void wireguard_tai64n_now(uint8_t *output)
{
  /* TODO Phase 2: implement with clock_gettime(CLOCK_REALTIME) */
  memset(output, 0, 12);
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
