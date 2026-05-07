set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlchemyViewer/alchemy-fonts
    REF ${VERSION}
    SHA512 5d6e0d7487f07573cf8135bb5c923609bf84409deb2b52a2fe74a8b6e55381211f857de774c8c658d5ed2e713a374d73b075d02abe9ac6ca8b07cd70266b8bd1
    HEAD_REF main
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/cascadia-code/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
)


file(INSTALL
    DIRECTORY "${SOURCE_PATH}/dejavu-sans/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/ibm-plex/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/noto-emoji/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/source-code/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
    PATTERN "*.md"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/source-han-sans/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/source-sans/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
    PATTERN "*.md"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/twemoji/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/fonts"
    FILES_MATCHING
    PATTERN "*.ttc"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.woff2"
    PATTERN "*.txt"
)

vcpkg_install_copyright(
    FILE_LIST
        ${SOURCE_PATH}/cascadia-code/CascadiaCode-License.txt
        ${SOURCE_PATH}/dejavu-sans/DejaVu-License.txt
        ${SOURCE_PATH}/ibm-plex/IBMPlex-LICENSE.txt
        ${SOURCE_PATH}/noto-emoji/NotoEmoji-LICENSE.txt
        ${SOURCE_PATH}/source-code/SourceCode.LICENSE.md
        ${SOURCE_PATH}/source-han-sans/SourceHanSans.txt
        ${SOURCE_PATH}/source-sans/SourceSans.LICENSE.md
        ${SOURCE_PATH}/twemoji/Twemoji-MIT-license.txt
        ${SOURCE_PATH}/twemoji/Twemoji-Artwork-CC-BY-license.txt
    COMMENT "Fonts contained within this package are licensed as follows"
    )
