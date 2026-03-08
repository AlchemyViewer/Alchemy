include_guard()

add_library(ll::fmt INTERFACE IMPORTED)

find_package(fmt CONFIG REQUIRED)
target_link_libraries(ll::fmt INTERFACE fmt::fmt)
