# -*- cmake -*-
include_guard()
add_library(ll::openal INTERFACE IMPORTED)

if (AL_USE_OPENAL)
    find_package(OpenAL CONFIG REQUIRED)
    target_link_libraries(ll::openal INTERFACE OpenAL::OpenAL)
    target_compile_definitions(ll::openal INTERFACE LL_OPENAL=1)
endif ()
