set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_FIXUP_MACHO_RPATH OFF)

vcpkg_from_github(
    OUT_SOURCE_PATH VLC_BIN_SOURCE
    REPO AlchemyViewer/vlc-bin
    REF 8c11c229cb127885d3f229dcd5778c901d033d14
    SHA512 09dd59497e44b1038bf6f46cb4a0640b2e4f79f16d9821886804ff2d206130c40726c3e5128778ac4d7a9977e93b4b92d44d83fb8e02d0122316494ac2644957
    HEAD_REF main
)

if(VCPKG_TARGET_IS_WINDOWS)
    set(VLC_DIR "${VLC_BIN_SOURCE}/vlc-win64")
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        set(VLC_DIR "${VLC_BIN_SOURCE}/vlc-macos-arm64")
    else()
        set(VLC_DIR "${VLC_BIN_SOURCE}/vlc-macos-intel64")
    endif()
endif()

file(INSTALL
    DIRECTORY "${VLC_DIR}/include/vlc/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/vlc"
    FILES_MATCHING
    PATTERN "*.h"
)

file(INSTALL
    DIRECTORY "${VLC_DIR}/lib/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
    FILES_MATCHING
    PATTERN "*.dylib"
    PATTERN "*.lib"
)

file(INSTALL
    DIRECTORY "${VLC_DIR}/plugins/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/plugins/${PORT}"
    FILES_MATCHING
    PATTERN "*.dll"
    PATTERN "*.so"
    PATTERN "*.dylib"
    PATTERN "*.dat"
)

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL
        DIRECTORY "${VLC_DIR}/bin/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/bin"
        FILES_MATCHING
        PATTERN "*.dll"
    )
endif()

vcpkg_install_copyright(FILE_LIST "${VLC_BIN_SOURCE}/LICENSE")
