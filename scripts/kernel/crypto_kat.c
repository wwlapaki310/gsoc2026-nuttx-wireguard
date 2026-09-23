/****************************************************************************
 * scripts/kernel/crypto_kat.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Known-answer tests for the other two WireGuard primitives NuttX already
 * ships and the in-kernel device relies on: X25519 (crypto/curve25519.c)
 * and BLAKE2s (crypto/blake2s.c). These are exercised end to end by the
 * live-peer interop tests (T1/T6); this runner pins them deterministically
 * with published vectors and needs no peer.
 *
 *   - X25519: RFC 7748 sections 5.2 (scalar * u) and 6.1 (Alice's
 *     scalar * base point). Both cross-checked against libsodium.
 *   - BLAKE2s-256: the unkeyed digests of "" and "abc" (BLAKE2 reference
 *     vectors, cross-checked against Python hashlib).
 *
 * Build (against the sim's crypto sources), e.g.:
 *   gcc -no-pie -fno-pie -I include -I . -I crypto -include nuttx/config.h \
 *       -fno-profile-arcs -fno-test-coverage \
 *       scripts/kernel/crypto_kat.c crypto/curve25519.c crypto/blake2s.c \
 *       -o /tmp/crypto_kat && /tmp/crypto_kat
 *
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <crypto/curve25519.h>
#include <nuttx/crypto/blake2s.h>

/* NuttX provides timingsafe_bcmp; curve25519.c calls it. The standalone
 * runner supplies its own so the crypto sources can be compiled in directly.
 */

int timingsafe_bcmp(const void *a, const void *b, size_t n)
{
  const unsigned char *pa = a;
  const unsigned char *pb = b;
  unsigned char r = 0;
  size_t i;
  for (i = 0; i < n; i++)
    {
      r |= pa[i] ^ pb[i];
    }

  return r != 0;
}

static void unhex(const char *hex, uint8_t *out, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
    {
      unsigned v;
      sscanf(hex + 2 * i, "%2x", &v);
      out[i] = (uint8_t)v;
    }
}

static int check(const char *what, const uint8_t *got,
                 const char *expect_hex, size_t n)
{
  uint8_t expect[64];
  unhex(expect_hex, expect, n);
  if (memcmp(got, expect, n) != 0)
    {
      printf("FAIL: %s\n", what);
      return 1;
    }

  return 0;
}

int main(void)
{
  int fails = 0;
  uint8_t out[32];
  int ret;

  /* X25519 RFC 7748 5.2: curve25519() clamps the scalar internally. */

  {
    uint8_t scalar[32];
    uint8_t u[32];
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
          scalar, 32);
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
          u, 32);
    ret = curve25519(out, scalar, u);
    if (ret == 0)
      {
        printf("FAIL: curve25519() returned 0 (RFC 7748 5.2)\n");
        fails++;
      }

    fails += check("x25519 RFC 7748 5.2", out,
        "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", 32);
  }

  /* X25519 RFC 7748 6.1: Alice's public = scalar * base point. */

  {
    uint8_t priv[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
          priv, 32);
    ret = curve25519_generate_public(out, priv);
    if (ret == 0)
      {
        printf("FAIL: curve25519_generate_public() returned 0 (RFC 7748 6.1)\n");
        fails++;
      }

    fails += check("x25519 RFC 7748 6.1 (keygen)", out,
        "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32);
  }

  /* BLAKE2s-256 unkeyed, "" and "abc". */

  {
    if (blake2s(out, 32, "", 0, NULL, 0) != 0)
      {
        printf("FAIL: blake2s('') returned nonzero\n");
        fails++;
      }

    fails += check("blake2s-256 empty", out,
        "69217a3079908094e11121d042354a7c1f55b6482ca1a51e1b250dfd1ed0eef9", 32);

    if (blake2s(out, 32, "abc", 3, NULL, 0) != 0)
      {
        printf("FAIL: blake2s('abc') returned nonzero\n");
        fails++;
      }

    fails += check("blake2s-256 abc", out,
        "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982", 32);
  }

  if (fails == 0)
    {
      printf("PASS: X25519 (RFC 7748 5.2/6.1) + BLAKE2s-256 KAT (TV)\n");
      return 0;
    }

  printf("FAIL: %d crypto KAT check(s) failed\n", fails);
  return 1;
}
