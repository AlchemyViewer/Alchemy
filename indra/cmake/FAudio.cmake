# -*- cmake -*-
include_guard()

if (USE_FAUDIO)
    add_library( ll::faudio INTERFACE IMPORTED )
    target_compile_definitions( ll::faudio INTERFACE LL_FAUDIO=1)

    find_package(FAudio CONFIG REQUIRED)
    target_link_libraries(ll::faudio INTERFACE FAudio::FAudio)
endif ()
