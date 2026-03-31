set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

if(VCPKG_TARGET_IS_WINDOWS)
    set(WEBRTC_LIBNAME "webrtc.lib")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/secondlife/3p-webrtc-build/releases/download/m137.7151.04.23/webrtc-m137.7151.04.23.22004231636-windows64-22004231636.tar.zst
        FILENAME webrtc-windows64.tar.zst
        SHA512 0b57ef8c863dd1e1c8c90bd9d63d93b57b4c690a8b30bc8417ad2cfac395d1ee6a6e7324470b5a220c96c2086446447119419300a66389a30285a2a73be5ff47
    )
elseif(VCPKG_TARGET_IS_OSX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/secondlife/3p-webrtc-build/releases/download/m137.7151.04.23/webrtc-m137.7151.04.23.22004231636-darwin64-22004231636.tar.zst
        FILENAME webrtc-osx.tar.zst
        SHA512 84f7c91b8d92ea3037bbf4370a8142d97260916c2ca62dad7641317f6a3da66154e12926a213f3ab3c5d96c5fc468643fc8aa13bca348efd0a0c8f080a52545d
    )
elseif(VCPKG_TARGET_IS_LINUX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/secondlife/3p-webrtc-build/releases/download/m137.7151.04.23/webrtc-m137.7151.04.23.22004231636-linux64-22004231636.tar.zst
        FILENAME webrtc-linux64.tar.zst
        SHA512 7f3a84ba9f6da66efe5ca1a475601df49bc0a80d8e9257765ff4a9720f07ccc6649d0421e5041ceaa322ea5d6059bbafcbc505b4cf7d13af22251fb76d799098
    )
endif()

vcpkg_extract_source_archive(
    WEBRTC_DIR
    ARCHIVE ${WEBRTC_ARCHIVE}
    NO_REMOVE_ONE_LEVEL
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include/")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib/")
file(RENAME "${WEBRTC_DIR}/include/webrtc/" "${CURRENT_PACKAGES_DIR}/include/webrtc/")
if (VCPKG_TARGET_IS_LINUX)
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/webrtc/build/linux/debian_bullseye_i386-sysroot")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/webrtc/build/linux/debian_bullseye_amd64-sysroot")
endif()

file(RENAME "${WEBRTC_DIR}/lib/release/${WEBRTC_LIBNAME}" "${CURRENT_PACKAGES_DIR}/lib/${WEBRTC_LIBNAME}")

vcpkg_install_copyright(FILE_LIST ${WEBRTC_DIR}/LICENSES/webrtc-license.txt)
