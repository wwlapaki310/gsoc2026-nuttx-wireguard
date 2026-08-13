############################################################################
# apps/netutils/wireguard/CMakeLists.txt
############################################################################

if(CONFIG_NET_WIREGUARD)
  # Protocol core (no OS dependencies, portable C)
  # NOTE: wireguardif.c (upstream lwIP API) is NOT used.
  # NuttX uses its own network stack. nuttx-wireguardif.c replaces it.
  target_sources(
    apps
    PRIVATE wireguard.c
            crypto.c
            crypto/refc/blake2s.c
            crypto/refc/chacha20.c
            crypto/refc/chacha20poly1305.c
            crypto/refc/poly1305-donna.c
            crypto/refc/x25519.c
            nuttx-platform.c
            nuttx-wireguardif.c)

  target_include_directories(apps PRIVATE ${CMAKE_CURRENT_LIST_DIR})

  # "wg" NSH builtin - brings wg0 up from Kconfig settings.
  nuttx_add_application(
    NAME
    ${CONFIG_NET_WIREGUARD_PROGNAME}
    PRIORITY
    ${CONFIG_NET_WIREGUARD_PRIORITY}
    STACKSIZE
    ${CONFIG_NET_WIREGUARD_STACKSIZE}
    SRCS
    wg_main.c
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR})
endif()
