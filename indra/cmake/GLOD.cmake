# -*- cmake -*-
include(Linking)
include(Prebuilt)

if (NOT USESYSTEMLIBS)
  use_prebuilt_binary(glod)
endif (NOT USESYSTEMLIBS)

set(GLODLIB ON CACHE BOOL "Using GLOD library")

set(GLOD_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include)
if (WINDOWS)
  set(GLOD_LIBRARIES 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/glod.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/glod.lib)
else ()
  set(GLOD_LIBRARIES GLOD)
endif ()
