#!/usr/bin/env bash
# TV (crypto KAT): build and run known-answer tests against NuttX's own
# crypto/ sources, the primitives the in-kernel WireGuard device uses.
# Deterministic, needs no peer.
#
#   1. ChaCha20-Poly1305 + XChaCha20-Poly1305 (crypto/chachapoly.c) — the
#      AEAD (data path) and the cookie-path AEAD; fails on a wrong nonce
#      layout (the bug fixed in this tree).
#   2. X25519 (crypto/curve25519.c) + BLAKE2s (crypto/blake2s.c) — the
#      handshake DH and hash.
#
# Compiles the crypto sources directly (not the coverage-instrumented sim
# objects) so it links with a plain host gcc.
set -euo pipefail

cd /opt/nuttx

cflags="-no-pie -fno-pie -I include -I . -I crypto -include nuttx/config.h \
    -fno-profile-arcs -fno-test-coverage"

# shellcheck disable=SC2086
gcc $cflags \
    /tmp/chachapoly_kat.c crypto/chachapoly.c crypto/poly1305.c \
    -o /tmp/chachapoly_kat
/tmp/chachapoly_kat

# shellcheck disable=SC2086
gcc $cflags \
    /tmp/crypto_kat.c crypto/curve25519.c crypto/blake2s.c \
    -o /tmp/crypto_kat
/tmp/crypto_kat
