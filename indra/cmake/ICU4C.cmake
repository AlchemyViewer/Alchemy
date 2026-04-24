# -*- cmake -*-
include_guard()

find_package(ICU REQUIRED COMPONENTS uc)

add_library(ll::icu INTERFACE IMPORTED)
target_link_libraries(ll::icu INTERFACE ICU::uc)
