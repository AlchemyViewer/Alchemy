set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

if(VCPKG_TARGET_IS_WINDOWS)
    set(WEBRTC_LIBNAME "webrtc.lib")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m${VERSION}/webrtc.windows_x86_64.tar.xz
        FILENAME webrtc.${VERSION}.windows_x86_64.tar.xz
        SHA512 965cf9ea22f60af1ca298873fbb961e6dc239187053f40a001b55dfed72d798fb2e3ff4054f98fafd6280b33e1a806186fbfc65d4dc2801ea6009b378f11bf30
    )
elseif(VCPKG_TARGET_IS_OSX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        vcpkg_download_distfile(
            WEBRTC_ARCHIVE
            URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m${VERSION}/webrtc.macos_arm64.tar.xz
            FILENAME webrtc.${VERSION}.macos_arm64.tar.xz
            SHA512 23b5caa7f675aa91d95f1dc99d324bf0d87f55e2181eaed6f6344c27ee12d8dd52602264cf4f2d0674631db5b57af74ee8892ff89e9114cbc85014977c8b3e91
        )
    else()
        vcpkg_download_distfile(
            WEBRTC_ARCHIVE
            URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m${VERSION}/webrtc.ubuntu-22.04_x86_64.tar.xz
            FILENAME webrtc.${VERSION}.macos_x86_64.tar.xz
            SHA512 0d232b2d59524307c50496e519d008250c74c6c00d0d9048932f3650a2bf7eccaa2ab9dcba55fc88943de080b9819817c27bd724a33c9b20bdc155e577addfc4
        )
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    set(WEBRTC_LIBNAME "libwebrtc.a")

    vcpkg_download_distfile(
        WEBRTC_ARCHIVE
        URLS https://github.com/AlchemyViewer/3p-webrtc/releases/download/m${VERSION}/webrtc.ubuntu-22.04_x86_64.tar.xz
        FILENAME webrtc.${VERSION}.ubuntu-22.04_x86_64.tar.xz
        SHA512 4c977f9b2df7761b833f307de6c92c1b21569feccf449522accd706af0c08c33afb2dfb3e55c61567b4f6a8c9a086fb862fbd0d2f45ec7a9e9428f2b88a537ab
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
