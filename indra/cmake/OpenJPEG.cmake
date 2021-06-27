# -*- cmake -*-
include(Prebuilt)

set(OPENJPEG_FIND_QUIETLY ON)
set(OPENJPEG_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindOpenJPEG)
else (USESYSTEMLIBS)
  use_prebuilt_binary(openjpeg)
  
  if(WINDOWS)
    set(OPENJPEG_LIBRARIES
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/openjp2.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/openjp2.lib)
  else(WINDOWS)
    set(OPENJPEG_LIBRARIES openjp2)
  endif(WINDOWS)
  
    set(OPENJPEG_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include/openjpeg)
endif (USESYSTEMLIBS)
