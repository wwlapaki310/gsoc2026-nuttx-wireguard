# crypto/chachapoly: chacha20poly1305 u64 nonce is placed in the wrong bytes

Found while bringing up the in-kernel WireGuard device against a real Linux
kernel WireGuard peer. Status: **draft, fixed locally in the fork, not
filed** (2026-09-20).

Suggested title: `crypto/chachapoly: place the 64-bit AEAD counter nonce in
the last 8 bytes (RFC 8439 / WireGuard convention)`

## Symptom

`chacha20poly1305_encrypt(dst, src, len, ad, adlen, nonce, key)` and its
`_decrypt` take the nonce as a `uint64_t` counter. Interop with any peer
that uses the RFC 8439 / WireGuard nonce layout works for counter 0 and
fails for every counter >= 1: the first packet of a session decrypts, the
rest fail authentication.

## Cause

`crypto/chachapoly.c` builds the 96-bit ChaCha20 nonce as

```c
uint64_t le_nonce = htole64(nonce);
uint8_t le_nonce_array[12];
explicit_bzero(le_nonce_array, sizeof(le_nonce_array));
memcpy(le_nonce_array, &le_nonce, sizeof(uint64_t));   /* bytes 0..7 */
```

so the counter lands in bytes 0..7 and bytes 8..11 are zero. `chacha_ivsetup()`
maps those 12 bytes to state words 13, 14, 15, giving a nonce of
`[counter_lo][counter_hi][0]`.

WireGuard (and the ChaCha20-Poly1305 the reference wireguard-lwip crypto
implements) puts the counter in the **last** 8 bytes, first 4 zero:
`[0][counter_lo][counter_hi]`. This matches wireguard-go, which does
`binary.LittleEndian.PutUint64(nonce[4:12], counter)`. For counter 0 the two
layouts are identical (all zero), which is why a handshake completes and
only transport data past the first packet breaks.

There is no in-tree caller of this AEAD (only the header declares it), so
the bug has never been exercised.

## Fix

Place the counter in bytes 4..11 in both `chacha20poly1305_encrypt` and
`chacha20poly1305_decrypt`:

```c
memcpy(le_nonce_array + 4, &le_nonce, sizeof(uint64_t));
```

With this the in-kernel WireGuard device handshakes and carries
bidirectional traffic against Linux kernel WireGuard on sim.

## Before filing

- Confirm against RFC 8439 test vectors expressed as a u64 counter, and
  add such a vector to `crypto/testmngr.c` (there was no chacha20poly1305
  KAT there). **Done, both as a standalone runner and in `testmngr.c`:**
  `scripts/kernel/chachapoly_kat.c` (+ `verify-sim-wg-kat.sh`) checks
  counters 0/1/2 against a pyca/cryptography reference, round-trips
  decryption, and rejects forged tags, run against `crypto/chachapoly.c`;
  it fails on the bytes-0..7 layout. The **same vectors are now added to
  `crypto/testmngr.c`/`testmngr.h`** (`test_chacha20poly1305`,
  `chacha20poly1305_tv_template`), wired into `crypto_test()` under
  `CONFIG_CRYPTO_ALGTEST`. Verified 2026-09-23: an ALGTEST sim build boots
  with `up_cryptoinitialize: crypto test OK`. (Fold this into the
  `crypto:` commit when assembling the PR; it currently lives in the fork
  working tree next to unrelated in-progress work.)
- `xchacha20poly1305_*` (24-byte nonce, WireGuard cookie replies) **is
  affected the same way and is repaired by the same fix** (checked
  2026-09-23). It does not take a byte nonce end-to-end: it derives an
  HChaCha20 subkey from `nonce[0:16]` and reads a u64 remainder from
  `nonce[16:24]` (`le64toh`) that it hands to the same
  `chacha20poly1305_encrypt(..., h_nonce, subkey)`. So the remainder lands
  in the inner nonce's bytes 4..11 only after the fix, giving the standard
  `0x00000000 || nonce[16:24]` XChaCha nonce; before the fix it landed in
  bytes 0..7 and broke interop too. The KAT runner now includes an
  XChaCha20-Poly1305 vector (libsodium / PyNaCl, draft-irtf-cfrg-xchacha
  prefix) that also exercises HChaCha20 — add it to `crypto/testmngr.c`
  alongside the chachapoly vectors. No separate xchacha code change is
  needed.
- This is a small, self-contained fix and belongs in a `crypto:` PR ahead
  of the WireGuard driver PR that depends on it.
