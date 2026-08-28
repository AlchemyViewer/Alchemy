include_guard()
add_library(ll::bugsplat INTERFACE IMPORTED)

if(USE_BUGSPLAT AND NOT LINUX)
    if(WINDOWS OR DARWIN)
        # bugsplat on Windows, bugsplat-apple on macOS; both ship the same
        # package and target.
        find_package(unofficial-bugsplat CONFIG REQUIRED)
        target_link_libraries(ll::bugsplat INTERFACE unofficial::bugsplat::bugsplat)
    else ()
        message(FATAL_ERROR "BugSplat is not supported; add -DUSE_BUGSPLAT=OFF")
    endif()

    if( NOT BUGSPLAT_DB )
        message(FATAL_ERROR "You need to set BUGSPLAT_DB when setting USE_BUGSPLAT")
    endif()

    target_compile_definitions(ll::bugsplat INTERFACE LL_BUGSPLAT=1)
endif()

if (USE_BUGSPLAT)
    if (BUGSPLAT_DB)
        message(STATUS "Building with BugSplat; database '${BUGSPLAT_DB}'")
    else (BUGSPLAT_DB)
        message(WARNING "Building with BugSplat, but no database name set (BUGSPLAT_DB)")
    endif (BUGSPLAT_DB)
else (USE_BUGSPLAT)
    message(STATUS "Not building with BugSplat")
endif (USE_BUGSPLAT)
