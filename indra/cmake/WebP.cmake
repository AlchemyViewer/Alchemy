# -*- cmake -*-
include_guard()
add_library(ll::libwebp INTERFACE IMPORTED)

find_package(WebP CONFIG REQUIRED)
target_link_libraries(ll::libwebp INTERFACE WebP::webp WebP::webpdecoder WebP::webpdemux)
