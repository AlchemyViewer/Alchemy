# -*- cmake -*-
include(Prebuilt)
include(GLH)
include(SDL2)

add_library( ll::glext INTERFACE IMPORTED )
if(NOT SDL_FOUND)
if (WINDOWS OR LINUX)
  use_system_binary(glext)
  use_prebuilt_binary(glext)
endif (WINDOWS OR LINUX)
endif()

