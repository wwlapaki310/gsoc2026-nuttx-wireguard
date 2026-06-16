############################################################################
# apps/netutils/wireguard/CMakeLists.txt
############################################################################

if(CONFIG_NET_WIREGUARD)
  # Protocol core (no OS dependencies, portable C).
  # wireguardif.c (upstream lwIP API) is NOT used — nuttx-wireguardif.c
  # replaces it with NuttX netdev + BSD socket integration.
  set(SRCS
      wireguard.c
      crypto.c
      crypto/refc/blake2s.c
      crypto/refc/chacha20.c
      crypto/refc/chacha20poly1305.c
      crypto/refc/poly1305-donna.c
      crypto/refc/x25519.c
      nuttx-platform.c
      nuttx-wireguardif.c)

  if(CONFIG_NET_WIREGUARD_APP)
    list(APPEND SRCS wg_app.c)
  endif()

  target_sources(apps PRIVATE ${SRCS})
  target_include_directories(apps PRIVATE ${CMAKE_CURRENT_LIST_DIR})
endif()
