# -*- cmake -*-
include(Linking)
include(Prebuilt)

set(WEBP_FIND_QUIETLY ON)
set(WEBP_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindWEBP)
else (USESYSTEMLIBS)
  use_prebuilt_binary(libwebp)
  if (WINDOWS)
    set(WEBP_LIBRARIES 
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libwebp_debug.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libwebp.lib
        )
  elseif(DARWIN)
    set(WEBP_LIBRARIES webp)
  else()
    set(WEBP_LIBRARIES webp)
  endif()
  set(WEBP_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/webp)
endif (USESYSTEMLIBS)
