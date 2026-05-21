# -*- cmake -*-
include_guard()
add_library(ll::SDL3 INTERFACE IMPORTED)

if(NOT USE_SDL_WINDOW)
    return()
endif()

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(ll::SDL3 INTERFACE SDL3::SDL3)

if(DARWIN)
  find_package(SDL3_image CONFIG REQUIRED)
  target_link_libraries(ll::SDL3 INTERFACE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
endif()
