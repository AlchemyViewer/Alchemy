# -*- cmake -*-
#
# The Discord Social SDK is downloaded per application from Discord's
# developer portal under its own licence, so its port, discord-social-sdk,
# lives in AlchemyViewer's private vcpkg registry: AL_USE_DISCORD asks for it
# through the manifest's discord feature, and the build needs read access to
# that registry.
include_guard()
add_library(ll::discord_sdk INTERFACE IMPORTED)

if(AL_USE_DISCORD)
  find_package(unofficial-discord-social-sdk CONFIG REQUIRED)

  target_compile_definitions(ll::discord_sdk INTERFACE LL_DISCORD=1)
  # A shared library on every platform; ViewerInstall.cmake ships it.
  target_link_libraries(
    ll::discord_sdk
    INTERFACE unofficial::discord-social-sdk::discord-social-sdk
  )
endif()
