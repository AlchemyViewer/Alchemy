# -*- cmake -*-
include_guard()
add_library(ll::libavif INTERFACE IMPORTED)

find_package(libavif CONFIG REQUIRED)
target_link_libraries(ll::libavif INTERFACE avif)
