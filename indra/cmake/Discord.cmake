# -*- cmake -*-
#
# The Discord Social SDK is downloaded per application from Discord's
# developer portal under its own licence, so it is not a port:
# AL_DISCORD_SDK_DIR names the unpacked SDK, which lays out as
# include/discordpp.h, lib/release/<library> and, on Windows,
# bin/release/discord_partner_sdk.dll.
include_guard()
add_library(ll::discord_sdk INTERFACE IMPORTED)

if (AL_USE_DISCORD)
  if (NOT AL_DISCORD_SDK_DIR)
    message(FATAL_ERROR "AL_USE_DISCORD needs the SDK: -DAL_DISCORD_SDK_DIR=<path to the unpacked Discord Social SDK>")
  endif ()

  find_path(DISCORD_SDK_INCLUDE_DIR discordpp.h
    PATHS "${AL_DISCORD_SDK_DIR}/include" NO_DEFAULT_PATH REQUIRED)
  find_library(DISCORD_SDK_LIBRARY discord_partner_sdk
    PATHS "${AL_DISCORD_SDK_DIR}/lib/release" NO_DEFAULT_PATH REQUIRED)

  # The library the viewer loads at run time, staged beside it by
  # Copy3rdPartyLibs and shipped by the manifest.
  if (WINDOWS)
    set(DISCORD_SDK_RUNTIME_DIR "${AL_DISCORD_SDK_DIR}/bin/release")
  else ()
    set(DISCORD_SDK_RUNTIME_DIR "${AL_DISCORD_SDK_DIR}/lib/release")
  endif ()

  target_compile_definitions(ll::discord_sdk INTERFACE LL_DISCORD=1)
  target_include_directories(ll::discord_sdk SYSTEM INTERFACE ${DISCORD_SDK_INCLUDE_DIR})
  target_link_libraries(ll::discord_sdk INTERFACE ${DISCORD_SDK_LIBRARY})
  message(VERBOSE "Using the Discord Social SDK from ${AL_DISCORD_SDK_DIR}")
endif ()
