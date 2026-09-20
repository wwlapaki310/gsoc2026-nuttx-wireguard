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
  add such a vector to `crypto/testmngr.c` (there is currently no
  chacha20poly1305 KAT there).
- Decide whether `xchacha20poly1305_*` (24-byte nonce, used by WireGuard
  cookie replies) needs the same review; its nonce is passed as bytes, so
  it is likely unaffected, but it is equally untested.
- This is a small, self-contained fix and belongs in a `crypto:` PR ahead
  of the WireGuard driver PR that depends on it.
