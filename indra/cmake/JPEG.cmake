# -*- cmake -*-
include(Prebuilt)

include(Linking)
set(JPEG_FIND_QUIETLY ON)
set(JPEG_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindJPEG)
else (USESYSTEMLIBS)
  use_prebuilt_binary(libjpeg-turbo)
  if (LINUX)
    set(JPEG_LIBRARIES jpeg)
  elseif (DARWIN)
    set(JPEG_LIBRARIES jpeg)
  elseif (WINDOWS)
    set(JPEG_LIBRARIES
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/jpeg.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/jpeg.lib)
  endif (LINUX)
  set(JPEG_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
endif (USESYSTEMLIBS)
