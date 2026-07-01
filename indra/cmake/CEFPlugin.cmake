# -*- cmake -*-
include_guard()
add_library(ll::cef INTERFACE IMPORTED)

if(DARWIN)
    find_library(APPKIT_LIBRARY AppKit REQUIRED)
    find_library(LIBCEF_DLL_WRAPPER_LIBRARY_RELEASE NAMES cef_dll_wrapper REQUIRED)
    find_library(LIBCEF_DLL_WRAPPER_LIBRARY_DEBUG NAMES cef_dll_wrapper PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
    target_link_libraries(ll::cef INTERFACE
        optimized ${LIBCEF_DLL_WRAPPER_LIBRARY_RELEASE}
        debug ${LIBCEF_DLL_WRAPPER_LIBRARY_DEBUG}
        ${APPKIT_LIBRARY}
       )
    set(CEF_FRAMEWORK_DIR "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/Chromium Embedded Framework.framework")
else()
    find_library(LIBCEF_LIBRARY_RELEASE NAMES cef libcef PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" REQUIRED NO_DEFAULT_PATH)
    find_library(LIBCEF_DLL_WRAPPER_LIBRARY_RELEASE NAMES cef_dll_wrapper PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" REQUIRED NO_DEFAULT_PATH)
    find_library(LIBCEF_LIBRARY_DEBUG NAMES cef libcef PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
    find_library(LIBCEF_DLL_WRAPPER_LIBRARY_DEBUG NAMES cef_dll_wrapper PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
    target_link_libraries(ll::cef INTERFACE
    optimized ${LIBCEF_DLL_WRAPPER_LIBRARY_RELEASE}
    debug ${LIBCEF_DLL_WRAPPER_LIBRARY_DEBUG}
    optimized ${LIBCEF_LIBRARY_RELEASE}
    debug ${LIBCEF_LIBRARY_DEBUG}
    )
endif()

set(CEF_INCLUDE_DIR "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/cef/include")
target_include_directories(ll::cef INTERFACE "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/cef" "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/cef/include")
