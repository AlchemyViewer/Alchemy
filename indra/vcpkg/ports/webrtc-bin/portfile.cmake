set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

if(VCPKG_TARGET_IS_WINDOWS)
    set(WEBRTC_LIBNAME "webrtc.lib")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r10/webrtc.windows_x86_64.tar.xz
        FILENAME webrtc.${VERSION}.windows_x86_64.tar.xz
        SHA512 f4215881234c9d2b64ee7c21ed5d321952ab1de73e9511846a994d9cc4d76d7e4dccbd3c5e87e7d2068f9871bee7833669d4a0a90cce4482a3d79905cf40bc97
    )
elseif(VCPKG_TARGET_IS_OSX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        vcpkg_download_distfile(
            WEBRTC_ARCHIVE
            URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r10/webrtc.macos_arm64.tar.xz
            FILENAME webrtc.${VERSION}.macos_arm64.tar.xz
            SHA512 64f0dd819f33a446c480a56897ffb928abdaedb745bd021f1efed50f790ad3ffbefb7b1655ba6f91ac6f7490fb4d4f66892d1882e03a27077a1f4078ffc97ab5
        )
    else()
        vcpkg_download_distfile(
            WEBRTC_ARCHIVE
            URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r10/webrtc.macos_x86_64.tar.xz
            FILENAME webrtc.${VERSION}.macos_x86_64.tar.xz
            SHA512 53e831d19c4bcd41145d7f183dfbe401da153dbc311ea8d26b3bdef146cb1349e7792a8b1f94098eebd88fe74801f465344167d6a8b06f99d06f01fa6286ea2e
        )
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m137.7151.04.20-r10/webrtc.ubuntu-22.04_x86_64.tar.xz
        FILENAME webrtc.${VERSION}.ubuntu-22.04_x86_64.tar.xz
        SHA512 7a7c248929055b0026cf9362188848a4e5624c9ceb149ae3b2f4a27c2e15e41c9063ac20a87c2a6e140fd3ad5c46b010f12be9ab076f383ed871c534bf51403e
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
