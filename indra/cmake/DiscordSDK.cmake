# -*- cmake -*-
include(Prebuilt)

include_guard()

#Discord Integration
if (DEFINED ENV{USE_DISCORD})
  set(USE_DISCORD $ENV{USE_DISCORD} CACHE BOOL "Enable Discord client integration" FORCE)
endif()

if(DEFINED ENV{DISCORD_CLIENTID})
  set(DISCORD_CLIENTID $ENV{DISCORD_CLIENTID} CACHE STRING "Discord Client ID" FORCE)
else()
  set(DISCORD_CLIENTID "" CACHE STRING "Discord Client ID")
endif()

if (INSTALL_PROPRIETARY AND NOT DISCORD_CLIENTID STREQUAL "")
  set(USE_DISCORD ON CACHE BOOL "Enable Discord client integration")
endif ()

if (USE_DISCORD)
    add_library( al::discord-gamesdk INTERFACE IMPORTED )

    use_prebuilt_binary(discord-gamesdk)    
    if (WINDOWS)
      target_link_libraries( al::discord-gamesdk INTERFACE
          ${ARCH_PREBUILT_DIRS_RELEASE}/discordgamesdk.lib
          ${ARCH_PREBUILT_DIRS_RELEASE}/discord_game_sdk.dll.lib)
    elseif (DARWIN)
      target_link_libraries( al::discord-gamesdk INTERFACE
          ${ARCH_PREBUILT_DIRS_RELEASE}/libdiscordgamesdk.a
          ${ARCH_PREBUILT_DIRS_RELEASE}/discord_game_sdk.dylib)
    elseif (LINUX)
      target_link_libraries( al::discord-gamesdk INTERFACE
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libdiscordgamesdk.a
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libdiscord_game_sdk.so )
          endif (WINDOWS)
    target_include_directories( al::discord-gamesdk SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/discord/)

    if(DISCORD_CLIENTID STREQUAL "")
        message(FATAL_ERROR "You must set a ClientID with -DDISCORD_CLIENTID= to enable Discord integration")
    endif()
    target_compile_definitions( al::discord-gamesdk INTERFACE AL_DISCORD=1 DISCORD_CLIENTID=${DISCORD_CLIENTID})
endif (USE_DISCORD)
