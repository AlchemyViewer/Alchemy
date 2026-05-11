set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlchemyViewer/alchemy-fonts
    REF ${VERSION}
    SHA512 a3e6581b5893d3d0b12d6ac9733e8d1b505a8c68d3688da6542145cf1cfc237c6094247ad6eddf7959ee5fcbb8b65199183ebc04bb79447f27ff10d4ed6f85f4
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
    DIRECTORY "${SOURCE_PATH}/inter/"
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
    DIRECTORY "${SOURCE_PATH}/opendyslexic/"
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
        ${SOURCE_PATH}/cascadia-code/CascadiaCode-LICENSE.txt
        ${SOURCE_PATH}/dejavu-sans/DejaVu-License.txt
        ${SOURCE_PATH}/ibm-plex/IBMPlex-LICENSE.txt
        ${SOURCE_PATH}/inter/Inter-LICENSE.txt
        ${SOURCE_PATH}/noto-emoji/NotoEmoji-LICENSE.txt
        ${SOURCE_PATH}/opendyslexic/OpenDyslexic-LICENSE.txt
        ${SOURCE_PATH}/source-code/SourceCode.LICENSE.md
        ${SOURCE_PATH}/source-han-sans/SourceHanSans.txt
        ${SOURCE_PATH}/source-sans/SourceSans.LICENSE.md
        ${SOURCE_PATH}/twemoji/Twemoji-MIT-license.txt
        ${SOURCE_PATH}/twemoji/Twemoji-Artwork-CC-BY-license.txt
    COMMENT "Fonts contained within this package are licensed as follows"
    )
