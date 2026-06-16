/****************************************************************************
 * apps/netutils/wireguard/wireguard-platform.h
 *
 * NuttX platform configuration for wireguard-lwip.
 *
 * This file overrides the upstream wireguard-platform.h from the
 * smartalock/wireguard-lwip repository. It is copied into the build tree
 * before the upstream sources, so it takes precedence.
 *
 * Differences from upstream defaults:
 *   WIREGUARD_MAX_PEERS    1 → 8  (upstream default is 1)
 *   WIREGUARD_MAX_SRC_IPS  2 → 5  (upstream default is 2)
 *
 * Copyright (c) 2021 Daniel Hope (www.floorsense.nz)
 * Modifications for NuttX: Satoru Akita (GSoC 2026)
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#ifndef WIREGUARD_PLATFORM_H
#define WIREGUARD_PLATFORM_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/****************************************************************************
 * WireGuard device limits
 *
 * WIREGUARD_MAX_PEERS     — peers stored per device (static allocation)
 * WIREGUARD_MAX_SRC_IPS   — allowed source IP ranges per peer
 * MAX_INITIATIONS_PER_SECOND — handshake DoS mitigation threshold
 *
 * Increase WIREGUARD_MAX_PEERS for multi-peer deployments.
 * Note: each peer costs ~2 KB of RAM (keypairs + handshake state).
 ****************************************************************************/

#ifndef WIREGUARD_MAX_PEERS
#  define WIREGUARD_MAX_PEERS          8
#endif

#ifndef WIREGUARD_MAX_SRC_IPS
#  define WIREGUARD_MAX_SRC_IPS        5
#endif

#ifndef MAX_INITIATIONS_PER_SECOND
#  define MAX_INITIATIONS_PER_SECOND   2
#endif

/****************************************************************************
 * Platform function declarations
 *
 * Implemented in nuttx-platform.c using NuttX POSIX APIs.
 ****************************************************************************/

/**
 * wireguard_sys_now - Monotonic millisecond clock.
 *
 * Returns milliseconds since an arbitrary fixed point (boot).
 * Used for handshake timeouts and keepalive scheduling.
 * Wraps every ~49 days (uint32_t overflow); wireguard.c handles wrap.
 */

uint32_t wireguard_sys_now(void);

/**
 * wireguard_random_bytes - Fill buffer with cryptographic random data.
 *
 * Must be backed by a CSPRNG. On NuttX: reads /dev/urandom.
 * Requires CONFIG_DEV_RANDOM=y.
 */

void wireguard_random_bytes(void *bytes, size_t size);

/**
 * wireguard_tai64n_now - Current time in TAI64N format (12 bytes).
 *
 * Output layout:
 *   [0..7]  seconds since TAI epoch (big-endian uint64, + 0x400000000000000a)
 *   [8..11] nanoseconds within the second (big-endian uint32)
 *
 * Used in handshake initiation messages for replay attack prevention.
 * On NuttX: clock_gettime(CLOCK_REALTIME).
 */

void wireguard_tai64n_now(uint8_t *output);

/**
 * wireguard_is_under_load - DoS mitigation: should we issue cookie replies?
 *
 * Return true to reject handshake initiations with a cookie challenge.
 * Embedded targets return false (resource exhaustion is not a concern
 * at the scale where WireGuard is deployed on NuttX).
 */

bool wireguard_is_under_load(void);

#endif /* WIREGUARD_PLATFORM_H */
