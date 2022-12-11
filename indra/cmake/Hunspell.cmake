# -*- cmake -*-
include(Linking)
include(Prebuilt)

set(HUNSPELL_FIND_QUIETLY ON)
set(HUNSPELL_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindHUNSPELL)
else (USESYSTEMLIBS)
  use_prebuilt_binary(libhunspell)
  if (WINDOWS)
    set(HUNSPELL_LIBRARY 
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libhunspell.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libhunspell.lib
        )
  elseif(DARWIN)
    set(HUNSPELL_LIBRARY 
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libhunspell-1.7.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libhunspell-1.7.a
        )
  elseif(LINUX)
    set(HUNSPELL_LIBRARY 
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libhunspell-1.7.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libhunspell-1.7.a
        )
  else()
    message(FATAL_ERROR "Invalid platform")
  endif()
  set(HUNSPELL_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/hunspell)
  use_prebuilt_binary(dictionaries)
endif (USESYSTEMLIBS)
