# -*- cmake -*-
include_guard()

add_library(ll::libvlc INTERFACE IMPORTED)

if(WINDOWS OR DARWIN)
    # Defines unofficial::libvlc::libvlc along with VLC_PLUGINS_DIR.
    find_package(unofficial-libvlc CONFIG REQUIRED)
    target_link_libraries(ll::libvlc INTERFACE unofficial::libvlc::libvlc)
else()
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(libvlc REQUIRED IMPORTED_TARGET libvlc)
    target_link_libraries(ll::libvlc INTERFACE PkgConfig::libvlc)
endif()
