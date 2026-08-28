# -*- cmake -*-
# Velopack installer and update framework integration
# https://velopack.io/

include_guard()

if(NOT USE_VELOPACK)
    return()
endif()

if (WINDOWS OR DARWIN)
    find_package(unofficial-velopack CONFIG REQUIRED)

    add_library(ll::velopack INTERFACE IMPORTED)
    target_link_libraries(ll::velopack INTERFACE unofficial::velopack::velopack)
    target_compile_definitions(ll::velopack INTERFACE LL_VELOPACK=1)
endif()
