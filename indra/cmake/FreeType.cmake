# -*- cmake -*-
include(Prebuilt)

include_guard()

option(USE_SYSTEM_FREETYPE "Enable usage of the AVX2 instruction set" OFF)

add_library( ll::freetype INTERFACE IMPORTED )

if(NOT USE_SYSTEM_FREETYPE)
    use_system_binary(freetype)
    use_prebuilt_binary(freetype)
    target_include_directories( ll::freetype SYSTEM INTERFACE  ${LIBS_PREBUILT_DIR}/include/freetype2/)
    if (WINDOWS)
        target_link_libraries( ll::freetype INTERFACE
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/freetyped.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/freetype.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/harfbuzz.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/harfbuzz.lib)
    elseif (LINUX)
        target_link_libraries( ll::freetype INTERFACE
        ${ARCH_PREBUILT_DIRS}/libfreetype.a
        ${ARCH_PREBUILT_DIRS}/libharfbuzz.a)
    else()
        target_link_libraries( ll::freetype INTERFACE
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libfreetyped.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libfreetype.a
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libharfbuzz.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libharfbuzz.a)
    endif()
endif()

if(LINUX)
    include(FindPkgConfig)

    if (USE_SYSTEM_FREETYPE)
        pkg_check_modules(freetype2 REQUIRED IMPORTED_TARGET freetype2)
        target_link_libraries( ll::freetype INTERFACE PkgConfig::freetype2)
    endif()

    pkg_check_modules(fontconfig REQUIRED IMPORTED_TARGET fontconfig)
    target_link_libraries( ll::freetype INTERFACE PkgConfig::fontconfig)
endif()
