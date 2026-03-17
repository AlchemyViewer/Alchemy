# -*- cmake -*-
# Velopack installer and update framework integration
# https://velopack.io/

include_guard()

if(NOT USE_VELOPACK)
    return()
endif()

if (WINDOWS)
    add_library(ll::velopack INTERFACE IMPORTED)

    target_include_directories(ll::velopack SYSTEM INTERFACE
        ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/velopack
    )

    find_library(VELOPACK_LIBRARY velopack_libc REQUIRED)
    target_link_libraries(ll::velopack INTERFACE ${VELOPACK_LIBRARY})

    # Windows system libraries required by Velopack
    target_link_libraries(ll::velopack INTERFACE
        winhttp
        ole32
        shell32
        shlwapi
        version
        userenv
        ws2_32
        bcrypt
        ntdll
    )

    target_compile_definitions(ll::velopack INTERFACE LL_VELOPACK=1)
elseif (DARWIN)
    add_library(ll::velopack INTERFACE IMPORTED)

    target_include_directories(ll::velopack SYSTEM INTERFACE
        ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/velopack
    )

    find_library(VELOPACK_LIBRARY velopack_libc REQUIRED)
    target_link_libraries(ll::velopack INTERFACE ${VELOPACK_LIBRARY})

    # macOS system frameworks required by Velopack (Rust static library dependencies)
    target_link_libraries(ll::velopack INTERFACE
        "-framework Foundation"
        "-framework Security"
        "-framework SystemConfiguration"
        "-framework AppKit"
        "-framework CoreFoundation"
        "-framework CoreServices"
        "-framework IOKit"
        "-liconv"
        "-lresolv"
    )

    target_compile_definitions(ll::velopack INTERFACE LL_VELOPACK=1)
endif()
