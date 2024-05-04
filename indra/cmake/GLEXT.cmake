# -*- cmake -*-
include(Prebuilt)
include(GLH)
include(GLM)
include(SDL2)

add_library( ll::glext INTERFACE IMPORTED )
if(NOT SDL_FOUND)
  use_system_binary(glext)
  use_prebuilt_binary(glext)
endif()

