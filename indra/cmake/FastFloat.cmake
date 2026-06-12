include_guard()

add_library(ll::fastfloat INTERFACE IMPORTED)

find_package(FastFloat CONFIG REQUIRED)
target_link_libraries(ll::fastfloat INTERFACE FastFloat::fast_float)
