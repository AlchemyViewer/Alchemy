# -*- cmake -*-
include(Prebuilt)
include(Linking)

set(CURL_FIND_QUIETLY ON)
set(CURL_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindCURL)
else (USESYSTEMLIBS)
  use_prebuilt_binary(curl)
  if (WINDOWS)
    set(CURL_LIBRARIES 
    debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcurld.lib
    optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcurl.lib)
  else (WINDOWS)
    set(CURL_LIBRARIES libcurl.a)
  endif (WINDOWS)
  set(CURL_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
endif (USESYSTEMLIBS)
