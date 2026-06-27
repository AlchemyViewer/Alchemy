set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

if(VCPKG_TARGET_IS_WINDOWS)
    set(WEBRTC_LIBNAME "webrtc.lib")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r11/webrtc.windows_x86_64.tar.xz
        FILENAME webrtc.${VERSION}.windows_x86_64.tar.xz
        SHA512 fa44150d94976c346501be8213689abb4220f4b75ae3ed5526776d9c6982337eaebf2396a5a8cf467e402e3e5707a952cd9f702e39d9129d480a514d12dc23f2
    )
elseif(VCPKG_TARGET_IS_OSX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        vcpkg_download_distfile(
            WEBRTC_ARCHIVE
            URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r11/webrtc.macos_arm64.tar.xz
            FILENAME webrtc.${VERSION}.macos_arm64.tar.xz
            SHA512 e67da2870f3b4b4031c89bd828f7b9705397423728d801314de0e17b95296535eaed5132b8dece50faa8f2d599851bc8ce3fd0bd2e911ec5f38d58d65664fb73
        )
    else()
        vcpkg_download_distfile(
            WEBRTC_ARCHIVE
            URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r11/webrtc.macos_x86_64.tar.xz
            FILENAME webrtc.${VERSION}.macos_x86_64.tar.xz
            SHA512 804a742da02f2e179a58f163de72db2b438c7a16ba82170b48dff78468a6145815db8566c9acbe22c342de523c2a9bb02a4fb4140e2a549bd0fdc069091b6d87
        )
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r11/webrtc.ubuntu-22.04_x86_64.tar.xz
        FILENAME webrtc.${VERSION}.ubuntu-22.04_x86_64.tar.xz
        SHA512 c20db361a98837dfb914821e97e0b813dc6856ebc2492521546f8cb4af45c820ac93d244c00973012d7ba3e84455c4bf80b291f5982785ed6b10cb55688a6f73
    )
endif()

vcpkg_extract_source_archive(
    WEBRTC_DIR
    ARCHIVE ${WEBRTC_ARCHIVE}
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include/")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib/")
file(RENAME "${WEBRTC_DIR}/include/" "${CURRENT_PACKAGES_DIR}/include/webrtc/")
if (VCPKG_TARGET_IS_LINUX)
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/webrtc/build/linux/debian_bullseye_i386-sysroot")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/webrtc/build/linux/debian_bullseye_amd64-sysroot")
endif()

file(RENAME "${WEBRTC_DIR}/lib/${WEBRTC_LIBNAME}" "${CURRENT_PACKAGES_DIR}/lib/${WEBRTC_LIBNAME}")

vcpkg_install_copyright(FILE_LIST ${WEBRTC_DIR}/NOTICE)
