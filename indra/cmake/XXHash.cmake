# -*- cmake -*-
include(Prebuilt)

if (USESYSTEMLIBS)
 find_package(xxHash 0.8.1 CONFIG REQUIRED)
else (USESYSTEMLIBS)
  use_prebuilt_binary(xxhash)
  
  if(WINDOWS)
    set(XXHASH_LIBRARIES
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/xxhash.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/xxhash.lib)
  else(WINDOWS)
    set(XXHASH_LIBRARIES xxhash)
  endif(WINDOWS)
  
  set(XXHASH_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/xxhash)
endif (USESYSTEMLIBS)
