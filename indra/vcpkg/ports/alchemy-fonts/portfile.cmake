vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlchemyViewer/alchemy-fonts
    REF 41951feb4197ec183dcafa69ecee32f8042cbcb6
    SHA512 93345a78cc3fbb1bd1446ba152d0f31fe61baddb4e262268099728b67536869fb0f853dac8235fe48b5c612abbc351ec4cfcdab05a6f9d5950bd4c6da7ca40a6
    HEAD_REF main
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/dejavu-sans/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.txt"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/twemoji/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.txt"
)

vcpkg_install_copyright(
    FILE_LIST
        ${SOURCE_PATH}/dejavu-sans/DejaVu-License.txt
        ${SOURCE_PATH}/twemoji/Twemoji-MIT-license.txt
        ${SOURCE_PATH}/twemoji/Twemoji-Artwork-CC-BY-license.txt
    COMMENT "Fonts contained within this package are licensed as follows"
    )
