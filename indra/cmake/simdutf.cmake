include_guard()

find_package(simdutf REQUIRED)

add_library(ll::simdutf INTERFACE IMPORTED)
target_link_libraries(ll::simdutf INTERFACE simdutf::simdutf)
