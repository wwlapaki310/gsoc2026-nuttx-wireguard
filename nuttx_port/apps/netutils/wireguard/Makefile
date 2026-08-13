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

  # "wg" NSH builtin - brings wg0 up from Kconfig settings (Phase 2 MVP;
  # "wg show"/"wg setconf" are a later phase).
  PROGNAME  = $(CONFIG_NET_WIREGUARD_PROGNAME)
  PRIORITY  = $(CONFIG_NET_WIREGUARD_PRIORITY)
  STACKSIZE = $(CONFIG_NET_WIREGUARD_STACKSIZE)
  MODULE    = $(CONFIG_NET_WIREGUARD)

  MAINSRC = wg_main.c
endif

include $(APPDIR)/Application.mk
