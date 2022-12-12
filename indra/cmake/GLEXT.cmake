# -*- cmake -*-
include(Prebuilt)
include(SDL2)

if (NOT USESYSTEMLIBS AND NOT SDL_FOUND)
  if (WINDOWS OR LINUX)
    use_prebuilt_binary(glext)
  endif (WINDOWS OR LINUX)
  use_prebuilt_binary(glh_linear)
  set(GLEXT_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include)
endif ()
