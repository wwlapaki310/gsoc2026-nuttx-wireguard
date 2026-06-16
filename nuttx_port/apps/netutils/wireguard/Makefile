############################################################################
# apps/netutils/wireguard/Makefile
############################################################################

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_NET_WIREGUARD),y)
  # Protocol core (wireguardif.c excluded — replaced by nuttx-wireguardif.c)
  CSRCS += wireguard.c
  CSRCS += crypto.c
  CSRCS += crypto/refc/blake2s.c
  CSRCS += crypto/refc/chacha20.c
  CSRCS += crypto/refc/chacha20poly1305.c
  CSRCS += crypto/refc/poly1305-donna.c
  CSRCS += crypto/refc/x25519.c
  CSRCS += nuttx-platform.c
  CSRCS += nuttx-wireguardif.c
ifeq ($(CONFIG_NET_WIREGUARD_APP),y)
  MAINSRC += wg_app.c
  PROGNAME += wg
  PRIORITY  = SCHED_PRIORITY_DEFAULT
  STACKSIZE = 4096
endif
endif

include $(APPDIR)/Application.mk
