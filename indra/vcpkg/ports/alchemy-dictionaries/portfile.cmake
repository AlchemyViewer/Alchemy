vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlchemyViewer/3p-dictionaries
    REF "v${VERSION}"
    SHA512 6ad31ce111cd672258cfe0b47b6513785daf4c7c2d7cd975125aec0ae2825b51fe3454c1c539d63ab47cc619d7e982c68eaf311004224852909db8e86061dae9
    HEAD_REF main
)

file(INSTALL
    ${SOURCE_PATH}/src/dictionaries.xml
    ${SOURCE_PATH}/src/sl.dic
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/de/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/en/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/es/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/fr/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/pt_br/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/ru/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL
    DIRECTORY "${SOURCE_PATH}/src/uk/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/dictionaries"
    FILES_MATCHING
    PATTERN "*.aff"
    PATTERN "*.dic"
)

file(INSTALL "${SOURCE_PATH}/src/en/en_us-dictionary-license.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
