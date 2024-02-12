# -*- cmake -*-
include(Prebuilt)

include_guard()
add_library( ll::freetype INTERFACE IMPORTED )

if(LINUX)
    include(FindPkgConfig)

    pkg_check_modules(freetype2 REQUIRED IMPORTED_TARGET freetype2)
    pkg_check_modules(fontconfig REQUIRED IMPORTED_TARGET fontconfig)
    target_link_libraries( ll::freetype INTERFACE PkgConfig::freetype2 PkgConfig::fontconfig)
    return()
endif()

use_system_binary(freetype)
use_prebuilt_binary(freetype)
target_include_directories( ll::freetype SYSTEM INTERFACE  ${LIBS_PREBUILT_DIR}/include/freetype2/)
if (WINDOWS)
    target_link_libraries( ll::freetype INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/freetyped.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/freetype.lib)
else()
    target_link_libraries( ll::freetype INTERFACE freetype)
endif()
