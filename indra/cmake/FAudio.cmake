# -*- cmake -*-
include_guard()
add_library(ll::faudio INTERFACE IMPORTED)

if (AL_USE_FAUDIO)
    find_package(FAudio CONFIG REQUIRED)
    target_link_libraries(ll::faudio INTERFACE FAudio::FAudio)
    target_compile_definitions(ll::faudio INTERFACE LL_FAUDIO=1)
endif ()
