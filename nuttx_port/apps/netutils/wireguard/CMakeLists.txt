############################################################################
# apps/netutils/wireguard/CMakeLists.txt
############################################################################

if(CONFIG_NET_WIREGUARD)
  # Protocol core (no OS dependencies, portable C)
  # NOTE: wireguardif.c (upstream lwIP API) is NOT used.
  # NuttX uses its own network stack. nuttx-wireguardif.c replaces it.
  set(WG_CORE_SRCS
      wireguard.c
      crypto.c
      crypto/refc/blake2s.c
      crypto/refc/chacha20.c
      crypto/refc/chacha20poly1305.c
      crypto/refc/poly1305-donna.c
      crypto/refc/x25519.c
      nuttx-platform.c
      nuttx-wireguardif.c)

  target_sources(apps PRIVATE ${WG_CORE_SRCS})

  target_include_directories(apps PRIVATE ${CMAKE_CURRENT_LIST_DIR})

  # The vendored reference crypto exports plain names (blake2s_init,
  # chacha20poly1305_encrypt, ...) that collide with NuttX's own crypto/
  # when CONFIG_CRYPTO is enabled; see the Makefile for the full story.
  # Prefix them at the preprocessor level, scoped to this module's sources
  # only, so the vendored files stay byte-identical to upstream.
  set(WG_CRYPTO_NS
      blake2s blake2s_init blake2s_update blake2s_final
      chacha20 chacha20_init hchacha20
      chacha20poly1305_encrypt chacha20poly1305_decrypt
      xchacha20poly1305_encrypt xchacha20poly1305_decrypt
      poly1305_init poly1305_update poly1305_finish
      x25519 X25519_BASE_POINT crypto_equal crypto_zero)
  set(WG_CRYPTO_DEFS)
  foreach(sym ${WG_CRYPTO_NS})
    list(APPEND WG_CRYPTO_DEFS "${sym}=wg_${sym}")
  endforeach()
  set_source_files_properties(${WG_CORE_SRCS} wg_main.c
                              PROPERTIES COMPILE_DEFINITIONS "${WG_CRYPTO_DEFS}")

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
