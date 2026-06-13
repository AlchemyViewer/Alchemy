# -*- cmake -*-
include_guard()
add_library(ll::SDL3 INTERFACE IMPORTED)

if(NOT USE_SDL_WINDOW)
    return()
endif()

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(ll::SDL3 INTERFACE SDL3::SDL3)

# SDL3_ttf renders the LLWindowSDL splash-screen text (see LLSplashScreenSDL).
find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(ll::SDL3 INTERFACE $<IF:$<TARGET_EXISTS:SDL3_ttf::SDL3_ttf-shared>,SDL3_ttf::SDL3_ttf-shared,SDL3_ttf::SDL3_ttf-static>)

# SDL3_image loads the branded splash icon PNG (and, on macOS, the cursor TIFFs).
find_package(SDL3_image CONFIG REQUIRED)
target_link_libraries(ll::SDL3 INTERFACE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
