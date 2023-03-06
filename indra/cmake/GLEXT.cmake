# -*- cmake -*-
include(Prebuilt)
include(SDL2)

if (NOT USESYSTEMLIBS AND NOT SDL_FOUND)
  use_prebuilt_binary(glext)
  use_prebuilt_binary(glh_linear)
  set(GLEXT_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include)
endif ()
