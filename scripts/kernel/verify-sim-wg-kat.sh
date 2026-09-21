#!/usr/bin/env bash
# TV (crypto KAT): build and run the u64-counter ChaCha20-Poly1305
# known-answer test against NuttX's own crypto/chachapoly.c, the AEAD the
# in-kernel WireGuard device uses. Deterministic, needs no peer. Fails on a
# wrong nonce layout (the bug fixed in this tree).
#
# Compiles the crypto sources directly (not the coverage-instrumented sim
# objects) so it links with a plain host gcc.
set -euo pipefail

cd /opt/nuttx

gcc -no-pie -fno-pie -I include -I . -I crypto -include nuttx/config.h \
    -fno-profile-arcs -fno-test-coverage \
    /tmp/chachapoly_kat.c crypto/chachapoly.c crypto/poly1305.c \
    -o /tmp/chachapoly_kat

/tmp/chachapoly_kat
