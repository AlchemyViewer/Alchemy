set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlchemyViewer/alchemy-fonts
    REF ${VERSION}
    SHA512 db35caf90f5eaf55e32bf266b00dbd1e1d1c2b7f80e6175c19342da1df4561a73735a35ef7f6e363b19d0d92e03069966a97b0764ca5bed277cdbf6f9f822475
    HEAD_REF main
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
        ${SOURCE_PATH}/inter/Inter-LICENSE.txt
        ${SOURCE_PATH}/dejavu-sans/DejaVu-License.txt
        ${SOURCE_PATH}/source-han-sans/SourceHanSans.txt
        ${SOURCE_PATH}/twemoji/Twemoji-MIT-license.txt
        ${SOURCE_PATH}/twemoji/Twemoji-Artwork-CC-BY-license.txt
    COMMENT "Fonts contained within this package are licensed as follows"
    )
