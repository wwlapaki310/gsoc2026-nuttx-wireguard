/****************************************************************************
 * scripts/kernel/chachapoly_kat.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * A known-answer test for NuttX's u64-counter ChaCha20-Poly1305
 * (crypto/chachapoly.c), the AEAD the in-kernel WireGuard device uses.
 * It exists to pin the nonce layout: the counter occupies bytes 4..11 of
 * the 96-bit nonce (RFC 8439 / WireGuard), first four bytes zero. Counter
 * 0 alone cannot catch a wrong layout (all-zero), so this checks counters
 * 0, 1 and 2 — a counter placed in bytes 0..7 (the bug fixed in this tree)
 * produces different ciphertext at counters >= 1 and fails here.
 *
 * Reference values were produced with pyca/cryptography's ChaCha20Poly1305
 * using nonce = b"\x00\x00\x00\x00" + counter.to_bytes(8, "little").
 *
 * This is a standalone runner for verification; the same vectors are meant
 * to go into crypto/testmngr.c as part of the crypto: nonce-fix PR.
 *
 * Build (against the sim's already-built crypto objects), e.g.:
 *   gcc -I include -I . scripts/kernel/chachapoly_kat.c \
 *       crypto/chachapoly.o crypto/poly1305.o -o /tmp/kat && /tmp/kat
 *
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <crypto/chachapoly.h>

/* NuttX provides timingsafe_bcmp; the standalone runner supplies its own so
 * the crypto sources can be compiled in directly. Not used by testmngr.
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

/* key = 00 01 .. 1f ; ad = the RFC 8439 AEAD example AAD ; pt = 40 bytes. */

static const uint8_t g_key[32] =
{
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t g_ad[12] =
{
  0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7
};

static const char g_pt[] = "Ladies and Gentlemen of the class of '99";
#define PTLEN (sizeof(g_pt) - 1)          /* 40, without the NUL */
#define CTLEN (PTLEN + 16)                /* ciphertext + tag    */

struct vec_s
{
  uint64_t counter;
  const char *ct_hex;                     /* ciphertext || tag */
};

static const struct vec_s g_vecs[] =
{
  { 0, "54d92658c89586b07d057c26ca2d3a4b9ddc969bc1c23d7c989099324959142f"
       "0128670bf1c90564559a59db860c02f4dd7f71757579c4fa" },
  { 1, "d3369736d0598f7947d28e1b37fbedeba0108e606704068db70cd07bee84bcd7"
       "ab6ab9cac4efe6b2ef843a642ea3f29e624bcc1a20b89358" },
  { 2, "e5c29a3c82998c0fa52b1f37a9a015d9a1fdc945a4dc29c1c9cf44f1aec84d86"
       "e33935d15c5ad1622be463752d854713f274ef7a8fe78079" },
};

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

int main(void)
{
  int fails = 0;
  size_t i;

  for (i = 0; i < sizeof(g_vecs) / sizeof(g_vecs[0]); i++)
    {
      uint8_t expect[CTLEN];
      uint8_t ct[CTLEN];
      uint8_t pt[PTLEN];
      int ok;

      unhex(g_vecs[i].ct_hex, expect, CTLEN);

      /* Encrypt must match the reference. */

      chacha20poly1305_encrypt(ct, (const uint8_t *)g_pt, PTLEN,
                               g_ad, sizeof(g_ad), g_vecs[i].counter, g_key);
      if (memcmp(ct, expect, CTLEN) != 0)
        {
          printf("FAIL: encrypt counter %llu\n",
                 (unsigned long long)g_vecs[i].counter);
          fails++;
        }

      /* Decrypt must round-trip. */

      ok = chacha20poly1305_decrypt(pt, expect, CTLEN, g_ad, sizeof(g_ad),
                                    g_vecs[i].counter, g_key);
      if (ok != 1 || memcmp(pt, g_pt, PTLEN) != 0)
        {
          printf("FAIL: decrypt counter %llu (ret=%d)\n",
                 (unsigned long long)g_vecs[i].counter, ok);
          fails++;
        }

      /* A flipped tag byte must be rejected. */

      unhex(g_vecs[i].ct_hex, expect, CTLEN);
      expect[CTLEN - 1] ^= 0x01;
      ok = chacha20poly1305_decrypt(pt, expect, CTLEN, g_ad, sizeof(g_ad),
                                    g_vecs[i].counter, g_key);
      if (ok == 1)
        {
          printf("FAIL: forged tag accepted at counter %llu\n",
                 (unsigned long long)g_vecs[i].counter);
          fails++;
        }
    }

  if (fails == 0)
    {
      printf("PASS: chacha20poly1305 u64-counter KAT (counters 0,1,2) (TV)\n");
      return 0;
    }

  printf("FAIL: %d chacha20poly1305 KAT check(s) failed\n", fails);
  return 1;
}
