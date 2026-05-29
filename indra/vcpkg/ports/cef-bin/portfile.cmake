set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_FIXUP_MACHO_RPATH OFF)
set(VCPKG_FIXUP_ELF_RPATH OFF)

if(VCPKG_TARGET_IS_WINDOWS)
    set(PLATFORM "windows64")
    set(CEF_URL "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_windows64_minimal.tar.bz2")
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        set(PLATFORM "macosarm64")
        set(CEF_URL "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_macosarm64_minimal.tar.bz2")
    else()
        set(PLATFORM "macosx64")
        set(CEF_URL "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_macosx64_minimal.tar.bz2")
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    set(PLATFORM "linux64")
    set(CEF_URL "https://cef-builds.spotifycdn.com/cef_binary_148.0.9%2Bg0d9d52a%2Bchromium-148.0.7778.180_linux64_minimal.tar.bz2")
endif()

vcpkg_download_distfile(ARCHIVE
    URLS ${CEF_URL}
    FILENAME "cef.${VERSION}.${PLATFORM}.tar.bz2"
    SHA512 3bb9f7cb5ee62bde7ac68e377341b7de3df64f32d8de1c2dbafd92da33146f8a5845ebbbe803e7660d921206e987ab651482e716357806cb30c9061a4f8684b5
)

vcpkg_extract_source_archive(
    CEF_SOURCE_PATH
    ARCHIVE ${ARCHIVE}
)

vcpkg_cmake_configure(
    SOURCE_PATH ${CEF_SOURCE_PATH}
)

vcpkg_cmake_build(
    TARGET libcef_dll_wrapper
)
vcpkg_copy_pdbs()

file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libcef_dll_wrapper/libcef_dll_wrapper.a" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${CEF_SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/cef")

if(VCPKG_TARGET_IS_OSX)
    set(CEF_RELEASE_FRAMEWORK_DIR "${CURRENT_PACKAGES_DIR}/lib/Chromium Embedded Framework.framework")
    file(RENAME "${CEF_SOURCE_PATH}/Release/Chromium Embedded Framework.framework" "${CEF_RELEASE_FRAMEWORK_DIR}")
else()
    file(INSTALL "${CEF_SOURCE_PATH}/Release" "${CEF_SOURCE_PATH}/Resources" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
endif()

file(INSTALL "${CEF_SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)