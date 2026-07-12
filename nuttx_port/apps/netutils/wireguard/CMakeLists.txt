############################################################################
# apps/netutils/wireguard/CMakeLists.txt
############################################################################

if(CONFIG_NET_WIREGUARD)

  # WireGuard protocol core (from wireguard-lwip, no OS dependencies)
  # NOTE: wireguardif.c (upstream lwIP API) is NOT used.
  # nuttx-wireguardif.c replaces it (TUN device + BSD sockets).

  nuttx_add_application(
    NAME
    ${CONFIG_NET_WIREGUARD_PROGNAME}
    PRIORITY
    ${CONFIG_NET_WIREGUARD_PRIORITY}
    STACKSIZE
    ${CONFIG_NET_WIREGUARD_STACKSIZE}
    MODULE
    ${CONFIG_NET_WIREGUARD}
    SRCS
    wg_main.c
    wireguard.c
    crypto.c
    crypto/refc/blake2s.c
    crypto/refc/chacha20.c
    crypto/refc/chacha20poly1305.c
    crypto/refc/poly1305-donna.c
    crypto/refc/x25519.c
    nuttx-platform.c
    nuttx-wireguardif.c
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR})

endif()
