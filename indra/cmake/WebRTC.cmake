# -*- cmake -*-
include_guard()

find_package(unofficial-webrtc CONFIG REQUIRED)

add_library(ll::webrtc INTERFACE IMPORTED)
target_link_libraries(ll::webrtc INTERFACE unofficial::webrtc::webrtc)

if (DARWIN)
    target_link_libraries(ll::webrtc INTERFACE ll::oslibraries)
endif ()
