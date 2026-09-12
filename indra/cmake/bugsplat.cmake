include_guard()
add_library(ll::bugsplat INTERFACE IMPORTED)

if(AL_USE_BUGSPLAT AND NOT LINUX)
    if(WINDOWS OR DARWIN)
        # bugsplat on Windows, bugsplat-apple on macOS; both ship the same
        # package and target.
        find_package(unofficial-bugsplat CONFIG REQUIRED)
        target_link_libraries(ll::bugsplat INTERFACE unofficial::bugsplat::bugsplat)
    else ()
        message(FATAL_ERROR "BugSplat is not supported; add -DAL_USE_BUGSPLAT=OFF")
    endif()

    if( NOT AL_BUGSPLAT_DATABASE )
        message(FATAL_ERROR "You need to set AL_BUGSPLAT_DATABASE when setting AL_USE_BUGSPLAT")
    endif()

    target_compile_definitions(ll::bugsplat INTERFACE LL_BUGSPLAT=1)
endif()

if (AL_USE_BUGSPLAT AND NOT AL_BUGSPLAT_DATABASE)
    message(WARNING "Building with BugSplat, but no database name set (AL_BUGSPLAT_DATABASE)")
endif ()
