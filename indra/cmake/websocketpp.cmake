# -*- cmake -*-
include_guard()

add_library(ll::websocketpp INTERFACE IMPORTED)

find_package(websocketpp CONFIG REQUIRED)

target_link_libraries(ll::websocketpp INTERFACE websocketpp::websocketpp)
