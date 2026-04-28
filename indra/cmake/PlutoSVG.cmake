# -*- cmake -*-
include_guard()
add_library(ll::plutosvg INTERFACE IMPORTED)

find_package(plutosvg CONFIG REQUIRED)
target_link_libraries(ll::plutosvg INTERFACE plutosvg::plutosvg)
