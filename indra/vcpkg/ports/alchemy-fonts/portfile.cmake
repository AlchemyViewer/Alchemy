vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlchemyViewer/alchemy-fonts
    REF b6f7421556d52b1bc58fa303ac8a212816fe014d
    SHA512 18733597fdc03262398a563c6f0bdc98558c1a7941615b0737475270192367a8c336de7cc1f841d30d576e22fb8008b68dcb362f25ad75e02919eff7aac2c1f0
    HEAD_REF main
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
        ${SOURCE_PATH}/dejavu-sans/DejaVu-License.txt
        ${SOURCE_PATH}/twemoji/Twemoji-MIT-license.txt
        ${SOURCE_PATH}/twemoji/Twemoji-Artwork-CC-BY-license.txt
    COMMENT "Fonts contained within this package are licensed as follows"
    )
