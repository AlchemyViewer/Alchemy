set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_FIXUP_MACHO_RPATH OFF)
set(VCPKG_FIXUP_ELF_RPATH OFF)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_LIBRARY_LINKAGE static)

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_windows64_minimal.tar.bz2"
        FILENAME "cef.${VERSION}.windows64.tar.bz2"
        SHA512 f38218298e44e7fedfa55438101a0d6ff11f03a95c13664fb5747089cdf25ff08be9648d03ae3b899c7e8bc2e8db17035cfbaf762213c47e63ce6fcc59349d20
    )
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        set(MACOS_ARCH_FLAG "-DPROJECT_ARCH=arm64")
        vcpkg_download_distfile(ARCHIVE
            URLS "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_macosarm64_minimal.tar.bz2"
            FILENAME "cef.${VERSION}.macosarm64.tar.bz2"
            SHA512 3bb9f7cb5ee62bde7ac68e377341b7de3df64f32d8de1c2dbafd92da33146f8a5845ebbbe803e7660d921206e987ab651482e716357806cb30c9061a4f8684b5
        )
    else()
        set(MACOS_ARCH_FLAG "-DPROJECT_ARCH=x86_64")
        vcpkg_download_distfile(ARCHIVE
            URLS "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_macosx64_minimal.tar.bz2"
            FILENAME "cef.${VERSION}.macosx64.tar.bz2"
            SHA512 4280392cdddc7524fcf38ddabef81386083a73c624d412deb88f6888534690327a0713b30abc65dab2379f5e5a53f06c445f665405716762bc34df867bc54001
        )
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_linux64_minimal.tar.bz2"
        FILENAME "cef.${VERSION}.linux64.tar.bz2"
        SHA512 197a1598f56b636db558d1f6dddfa3885f529571d354c0ff98dd27dbea56e73790f65ddece89e130bdc76611e374de8da5282e517980ffac98ef59037422e390
    )
endif()

vcpkg_extract_source_archive(
    CEF_SOURCE_PATH
    ARCHIVE ${ARCHIVE}
)

if(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_CRT_LINKAGE MATCHES "dynamic")
        vcpkg_cmake_configure(
            SOURCE_PATH ${CEF_SOURCE_PATH}
            OPTIONS
                -DCEF_RUNTIME_LIBRARY_FLAG="/MD"
        )
    else()
        vcpkg_cmake_configure(
            SOURCE_PATH ${CEF_SOURCE_PATH}
        )
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    vcpkg_cmake_configure(
        SOURCE_PATH ${CEF_SOURCE_PATH}
        OPTIONS
            ${MACOS_ARCH_FLAG}
    )
else()
    vcpkg_cmake_configure(
        SOURCE_PATH ${CEF_SOURCE_PATH}
    )
endif()

vcpkg_cmake_build(
    TARGET libcef_dll_wrapper
)
vcpkg_copy_pdbs()

file(INSTALL "${CEF_SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/cef/include")

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libcef_dll_wrapper/libcef_dll_wrapper.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
else()
    file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libcef_dll_wrapper/libcef_dll_wrapper.a" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
endif()

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${CEF_SOURCE_PATH}/Release/libcef.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL "${CEF_SOURCE_PATH}/Release/libcef.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL
        DIRECTORY "${CEF_SOURCE_PATH}/Release/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Release"
        FILES_MATCHING
        PATTERN "*.*"
        PATTERN "libcef.dll" EXCLUDE
        PATTERN "libcef.lib" EXCLUDE
    )
    file(INSTALL "${CEF_SOURCE_PATH}/Resources" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
elseif(VCPKG_TARGET_IS_OSX)
    set(CEF_RELEASE_FRAMEWORK_DIR "${CURRENT_PACKAGES_DIR}/lib/Chromium Embedded Framework.framework")
    file(RENAME "${CEF_SOURCE_PATH}/Release/Chromium Embedded Framework.framework" "${CEF_RELEASE_FRAMEWORK_DIR}")
elseif(VCPKG_TARGET_IS_LINUX)
    file(INSTALL "${CEF_SOURCE_PATH}/Release/libcef.so" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL
        DIRECTORY "${CEF_SOURCE_PATH}/Release/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Release"
        FILES_MATCHING
        PATTERN "*.*"
        PATTERN "chrome-sandbox"
        PATTERN "libcef.so" EXCLUDE
    )
    file(INSTALL "${CEF_SOURCE_PATH}/Resources" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
endif()

file(INSTALL "${CEF_SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
