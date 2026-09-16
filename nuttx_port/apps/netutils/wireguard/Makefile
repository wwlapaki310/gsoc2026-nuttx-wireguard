############################################################################
# apps/netutils/wireguard/Makefile
############################################################################

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_NET_WIREGUARD),y)
  # Protocol core (no OS dependencies, portable C)
  CSRCS += wireguard.c
  CSRCS += crypto.c
  CSRCS += crypto/refc/blake2s.c
  CSRCS += crypto/refc/chacha20.c
  CSRCS += crypto/refc/chacha20poly1305.c
  CSRCS += crypto/refc/poly1305-donna.c
  CSRCS += crypto/refc/x25519.c
  # NuttX platform layer
  CSRCS += nuttx-platform.c
  CSRCS += nuttx-wireguardif.c
  # NOTE: wireguardif.c (upstream lwIP API) is NOT used.
  # NuttX uses its own network stack. nuttx-wireguardif.c replaces it.

  # The vendored reference crypto exports plain names (blake2s_init,
  # chacha20poly1305_encrypt, ...). NuttX's own crypto/ (CONFIG_CRYPTO, pulled
  # in for example by CONFIG_CRYPTO_RANDOM_POOL) exports the same names with
  # different signatures and state structs. Both end up in the link, the
  # archive met first wins silently, and the other caller corrupts its stack
  # (seen as a jump to a garbage PC inside wireguard_init() on Spresense).
  # Prefix ours at the preprocessor level so the vendored sources stay
  # byte-identical to upstream.
  WG_CRYPTO_NS  = blake2s blake2s_init blake2s_update blake2s_final
  WG_CRYPTO_NS += chacha20 chacha20_init hchacha20
  WG_CRYPTO_NS += chacha20poly1305_encrypt chacha20poly1305_decrypt
  WG_CRYPTO_NS += xchacha20poly1305_encrypt xchacha20poly1305_decrypt
  WG_CRYPTO_NS += poly1305_init poly1305_update poly1305_finish
  WG_CRYPTO_NS += x25519 X25519_BASE_POINT crypto_equal crypto_zero
  CFLAGS += $(foreach s,$(WG_CRYPTO_NS),-D$(s)=wg_$(s))

  # "wg" NSH builtin, following the upstream wg(8) command surface:
  # up/down, show, showconf/setconf, genkey/pubkey and set.
  PROGNAME  = $(CONFIG_NET_WIREGUARD_PROGNAME)
  PRIORITY  = $(CONFIG_NET_WIREGUARD_PRIORITY)
  STACKSIZE = $(CONFIG_NET_WIREGUARD_STACKSIZE)
  MODULE    = $(CONFIG_NET_WIREGUARD)

  MAINSRC = wg_main.c
endif

include $(APPDIR)/Application.mk
