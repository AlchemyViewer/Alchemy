# -*- cmake -*-
include(Prebuilt)

if (NOT USESYSTEMLIBS)
  set(GLEXT_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include)
endif (NOT USESYSTEMLIBS)
