include_guard()

find_package(simdjson CONFIG REQUIRED)

add_library(ll::simdjson INTERFACE IMPORTED)
target_link_libraries(ll::simdjson INTERFACE simdjson::simdjson)
