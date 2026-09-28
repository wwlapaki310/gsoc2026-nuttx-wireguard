/****************************************************************************
 * scripts/kernel/wg_crypto_kat.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Known-answer tests for the WireGuard key derivation the *driver* owns:
 * HMAC-BLAKE2s and the HKDF chain in drivers/net/wireguard/wg_crypto.c.
 *
 * The other KAT runners cover primitives NuttX already ships (ChaCha20-
 * Poly1305, X25519, BLAKE2s). This one covers the layer above them, which is
 * this port's own code, and which the live-peer tests exercise only end to
 * end: if the HKDF chain were wrong, T1 would fail somewhere in the handshake
 * without saying where.
 *
 * Vectors were computed independently with Python's hmac + hashlib.blake2s.
 * That is a real cross-check rather than a restatement: the reference uses a
 * different BLAKE2s implementation, and RFC 2104 framing (block size, the
 * ipad/opad constants, hashing a key longer than the block) is exactly the
 * kind of detail that interoperates by accident until it does not.
 *
 * The two constants from whitepaper 5.4 are checked as well -- Hash(
 * Construction) and Hash(Hash(Construction) || Identifier) -- recomputed here
 * from the same strings the driver uses, since wg_noise.c keeps its copies
 * static.
 *
 * Build (against the sim's sources), e.g.:
 *   gcc -no-pie -fno-pie -I include -I . -I crypto \
 *       -I drivers/net/wireguard -include nuttx/config.h \
 *       -fno-profile-arcs -fno-test-coverage \
 *       scripts/kernel/wg_crypto_kat.c drivers/net/wireguard/wg_crypto.c \
 *       crypto/blake2s.c crypto/chachapoly.c crypto/poly1305.c \
 *       crypto/curve25519.c -o /tmp/wg_crypto_kat && /tmp/wg_crypto_kat
 *
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <nuttx/clock.h>

#include "wg_crypto.h"

/****************************************************************************
 * Stubs
 *
 * wg_crypto.c is compiled in whole, so the few OS facilities it references
 * from paths this runner does not call have to resolve. None of them are
 * reached by the tests below.
 ****************************************************************************/

/* wg_random() and wg_now(); neither is on the derivation path under test. */

void up_rngbuf(void *buf, size_t buflen)
{
  memset(buf, 0, buflen);
}

clock_t clock_systime_ticks(void)
{
  return 0;
}

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

/****************************************************************************
 * Vectors
 ****************************************************************************/

static const uint8_t g_construction[37] =
  "Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
static const uint8_t g_identifier[34] = "WireGuard v1 zx2c4 Jason@zx2c4.com";
static const uint8_t g_label_mac1[8] = "mac1----";

static const uint8_t g_construction_hash[32] =
{
  0x60, 0xe2, 0x6d, 0xae, 0xf3, 0x27, 0xef, 0xc0, 0x2e, 0xc3, 0x35,
  0xe2, 0xa0, 0x25, 0xd2, 0xd0, 0x16, 0xeb, 0x42, 0x06, 0xf8, 0x72,
  0x77, 0xf5, 0x2d, 0x38, 0xd1, 0x98, 0x8b, 0x78, 0xcd, 0x36
};

static const uint8_t g_identifier_hash[32] =
{
  0x22, 0x11, 0xb3, 0x61, 0x08, 0x1a, 0xc5, 0x66, 0x69, 0x12, 0x43,
  0xdb, 0x45, 0x8a, 0xd5, 0x32, 0x2d, 0x9c, 0x6c, 0x66, 0x22, 0x93,
  0xe8, 0xb7, 0x0e, 0xe1, 0x9c, 0x65, 0xba, 0x07, 0x9e, 0xf3
};

/* HMAC-BLAKE2s with a 32-byte key, and with a 100-byte key, which RFC 2104
 * requires be hashed down to the digest first. The long-key path has no
 * effect on interoperability until a caller uses one, so it is easy to get
 * wrong and never notice.
 */

static const uint8_t g_hmac_msg[] = "wireguard hmac-blake2s vector";

static const uint8_t g_hmac_out[32] =
{
  0x8c, 0x4c, 0x06, 0x3b, 0x53, 0x3b, 0x2d, 0x34, 0x80, 0x0d, 0x14,
  0x82, 0x00, 0xab, 0x6e, 0xf9, 0x8f, 0x87, 0x75, 0x1f, 0x67, 0x3a,
  0x8c, 0x13, 0xaf, 0x5d, 0x2e, 0xe3, 0x79, 0x4e, 0xcd, 0x26
};

static const uint8_t g_hmac_longkey_out[32] =
{
  0x21, 0x38, 0x1b, 0xd6, 0xd3, 0xe1, 0x58, 0x7a, 0xcc, 0x6e, 0x92,
  0x4c, 0x71, 0x24, 0x88, 0x8f, 0x27, 0xa8, 0xaa, 0x41, 0x9e, 0xa1,
  0xab, 0x21, 0xc2, 0xd9, 0x59, 0xdd, 0xae, 0x43, 0x42, 0xe4
};

/* The HKDF chain, keyed with Hash(Construction) as a handshake would be. */

static const uint8_t g_kdf_data[32] =
{
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
  0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
  0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf
};

static const uint8_t g_kdf_t1[32] =
{
  0x01, 0xc6, 0x0c, 0x3c, 0xca, 0x9f, 0x14, 0x0e, 0x11, 0xe5, 0x5e,
  0x30, 0x2d, 0x1d, 0x34, 0xc3, 0xa8, 0x12, 0x78, 0x1c, 0x5a, 0x81,
  0x37, 0xb2, 0x16, 0x4e, 0x4f, 0xb3, 0xf7, 0x94, 0x0b, 0x12
};

static const uint8_t g_kdf_t2[32] =
{
  0x0d, 0x62, 0x64, 0x10, 0x54, 0xb1, 0x3a, 0x75, 0x42, 0x18, 0x48,
  0xb6, 0x79, 0x47, 0xd5, 0x71, 0xd0, 0x17, 0x36, 0xe9, 0x35, 0x6b,
  0xa4, 0x1e, 0xdf, 0x25, 0x0b, 0xc8, 0x16, 0xf7, 0xcc, 0x45
};

static const uint8_t g_kdf_t3[32] =
{
  0x27, 0xbc, 0x69, 0x78, 0xc9, 0x96, 0x52, 0xc1, 0x9d, 0x54, 0xc6,
  0xc6, 0x74, 0x86, 0xf1, 0x95, 0x63, 0xd8, 0xef, 0x8a, 0xdd, 0x6b,
  0x92, 0xb4, 0xf8, 0xf0, 0x60, 0xd2, 0x4b, 0x0b, 0x6a, 0x0d
};

/* Hash(Label-Mac1 || Spub), the key mac1 is computed under. */

static const uint8_t g_mac1_pub[32] =
{
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
  0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
  0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
};

static const uint8_t g_mac1_key[32] =
{
  0x06, 0x10, 0xc8, 0x8c, 0x43, 0x4d, 0x08, 0xc6, 0xec, 0x4e, 0x70,
  0x1b, 0x3a, 0x60, 0x5c, 0x2d, 0xbb, 0x7f, 0xf8, 0x52, 0x81, 0xa6,
  0x97, 0xb2, 0x06, 0xb4, 0x10, 0x07, 0x78, 0xdc, 0x11, 0x18
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int g_failures;

static void expect(const char *what, const uint8_t *got,
                   const uint8_t *want, size_t len)
{
  size_t i;

  if (memcmp(got, want, len) == 0)
    {
      printf("  ok   %s\n", what);
      return;
    }

  printf("  FAIL %s\n       got  ", what);
  for (i = 0; i < len; i++)
    {
      printf("%02x", got[i]);
    }

  printf("\n       want ");
  for (i = 0; i < len; i++)
    {
      printf("%02x", want[i]);
    }

  printf("\n");
  g_failures++;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  uint8_t out[WG_HASH_LEN];
  uint8_t t1[WG_HASH_LEN];
  uint8_t t2[WG_HASH_LEN];
  uint8_t t3[WG_HASH_LEN];
  uint8_t key[100];
  uint8_t buf[WG_HASH_LEN + sizeof(g_identifier)];
  uint8_t label[sizeof(g_label_mac1) + WG_KEY_LEN];
  size_t msglen = sizeof(g_hmac_msg) - 1;   /* Without the NUL. */
  size_t i;

  printf("WireGuard key-derivation KAT (drivers/net/wireguard/wg_crypto.c)\n");

  /* Whitepaper 5.4 constants, built the way wg_noise_init does. */

  wg_hash(out, g_construction, sizeof(g_construction));
  expect("Hash(Construction)", out, g_construction_hash, WG_HASH_LEN);

  memcpy(buf, out, WG_HASH_LEN);
  memcpy(buf + WG_HASH_LEN, g_identifier, sizeof(g_identifier));
  wg_hash(out, buf, sizeof(buf));
  expect("Hash(Hash(Construction) || Identifier)", out,
         g_identifier_hash, WG_HASH_LEN);

  /* HMAC-BLAKE2s, short key and a key longer than the 64-byte block. */

  for (i = 0; i < WG_HASH_LEN; i++)
    {
      key[i] = (uint8_t)i;
    }

  wg_hmac(out, key, WG_HASH_LEN, g_hmac_msg, msglen);
  expect("HMAC-BLAKE2s, 32-byte key", out, g_hmac_out, WG_HASH_LEN);

  for (i = 0; i < sizeof(key); i++)
    {
      key[i] = (uint8_t)((i * 7) & 0xff);
    }

  wg_hmac(out, key, sizeof(key), g_hmac_msg, msglen);
  expect("HMAC-BLAKE2s, 100-byte key (RFC 2104 hashes it first)", out,
         g_hmac_longkey_out, WG_HASH_LEN);

  /* The HKDF chain. kdf2 and kdf3 must agree with kdf1 on t1, and with each
   * other on t2 -- a chain that only matches at its last output would still
   * break a handshake halfway through.
   */

  wg_kdf1(t1, g_construction_hash, g_kdf_data, sizeof(g_kdf_data));
  expect("kdf1 -> t1", t1, g_kdf_t1, WG_HASH_LEN);

  memset(t1, 0, sizeof(t1));
  wg_kdf2(t1, t2, g_construction_hash, g_kdf_data, sizeof(g_kdf_data));
  expect("kdf2 -> t1", t1, g_kdf_t1, WG_HASH_LEN);
  expect("kdf2 -> t2", t2, g_kdf_t2, WG_HASH_LEN);

  memset(t1, 0, sizeof(t1));
  memset(t2, 0, sizeof(t2));
  wg_kdf3(t1, t2, t3, g_construction_hash, g_kdf_data, sizeof(g_kdf_data));
  expect("kdf3 -> t1", t1, g_kdf_t1, WG_HASH_LEN);
  expect("kdf3 -> t2", t2, g_kdf_t2, WG_HASH_LEN);
  expect("kdf3 -> t3", t3, g_kdf_t3, WG_HASH_LEN);

  /* Hash(Label-Mac1 || Spub). */

  memcpy(label, g_label_mac1, sizeof(g_label_mac1));
  memcpy(label + sizeof(g_label_mac1), g_mac1_pub, WG_KEY_LEN);
  wg_hash(out, label, sizeof(label));
  expect("Hash(Label-Mac1 || Spub)", out, g_mac1_key, WG_HASH_LEN);

  if (g_failures != 0)
    {
      printf("FAIL: %d key-derivation vector(s) did not match\n", g_failures);
      return 1;
    }

  printf("PASS: HMAC-BLAKE2s, the HKDF chain, the 5.4 constants and the "
         "mac1 label key\n");
  return 0;
}
