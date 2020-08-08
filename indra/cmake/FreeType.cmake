# -*- cmake -*-
include(Prebuilt)

if (LINUX OR USESYSTEMLIBS)
  include(FindPkgConfig)

  pkg_check_modules(FREETYPE REQUIRED freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
else ()
  use_prebuilt_binary(freetype)
  set(FREETYPE_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/freetype2/)
  set(FREETYPE_LIBRARIES freetype)
endif ()

link_directories(${FREETYPE_LIBRARY_DIRS})
