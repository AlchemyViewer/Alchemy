# -*- cmake -*-
include(Linking)
include(Prebuilt)
include(Variables)

if (USE_DISCORD)
  if (STANDALONE)
    # In that case, we use the version of the library installed on the system
    set(DISCORD_FIND_REQUIRED ON)
    include(FindFMODSTUDIO)
  else (STANDALONE)
    use_prebuilt_binary(discord-gamesdk)    
    if (WINDOWS)
      set(DISCORD_LIBRARY 
          ${ARCH_PREBUILT_DIRS_RELEASE}/discordgamesdk.lib
          ${ARCH_PREBUILT_DIRS_RELEASE}/discord_game_sdk.dll.lib)
    elseif (DARWIN)
      set(DISCORD_LIBRARY 
          ${ARCH_PREBUILT_DIRS_RELEASE}/libdiscordgamesdk.a
          ${ARCH_PREBUILT_DIRS_RELEASE}/discord_game_sdk.dylib)
    elseif (LINUX)
      set(DISCORD_LIBRARY 
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libdiscord_game_sdk.so
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libdiscordgamesdk.a)
    endif (WINDOWS)
    set(DISCORD_LIBRARIES ${DISCORD_LIBRARY})
    set(DISCORD_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/discord/)

    add_definitions(-DUSE_DISCORD=1)
  endif (STANDALONE)

  if(DISCORD_CLIENTID STREQUAL "")
    message(FATAL_ERROR "You must set a ClientID with -DDISCORD_CLIENTID= to enable Discord integration")
  endif()

endif (USE_DISCORD)

