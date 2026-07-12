############################################################################
# apps/netutils/wireguard/Makefile
############################################################################

include $(APPDIR)/Make.defs

# WireGuard protocol core (from wireguard-lwip, no OS dependencies)

CSRCS += wireguard.c
CSRCS += crypto.c
CSRCS += crypto/refc/blake2s.c
CSRCS += crypto/refc/chacha20.c
CSRCS += crypto/refc/chacha20poly1305.c
CSRCS += crypto/refc/poly1305-donna.c
CSRCS += crypto/refc/x25519.c

# NuttX platform + network interface layer
# NOTE: wireguardif.c (upstream lwIP API) is NOT used.
# nuttx-wireguardif.c replaces it (TUN device + BSD sockets).

CSRCS += nuttx-platform.c
CSRCS += nuttx-wireguardif.c

# NSH "wg" command

PROGNAME  = $(CONFIG_NET_WIREGUARD_PROGNAME)
PRIORITY  = $(CONFIG_NET_WIREGUARD_PRIORITY)
STACKSIZE = $(CONFIG_NET_WIREGUARD_STACKSIZE)
MODULE    = $(CONFIG_NET_WIREGUARD)

MAINSRC = wg_main.c

include $(APPDIR)/Application.mk
