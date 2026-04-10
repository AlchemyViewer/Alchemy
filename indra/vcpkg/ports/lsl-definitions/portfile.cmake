set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(
    LSL_ARCHIVE
    URLS "https://github.com/secondlife/lsl-definitions/releases/download/v${VERSION}/lsl-definitions.zip"
    FILENAME lsl-definitions.${VERSION}.zip
    SHA512 683fe6ced78863fdf24e552ce029f48753d3115a4d3f74df9067d81150abf4ef9b27a78674e4216d522fb3757a1c880e26ff517baec0a4c264ddb366c4dff8d4
)

vcpkg_extract_source_archive(LSL_DIR ARCHIVE ${LSL_ARCHIVE} NO_REMOVE_ONE_LEVEL)

file(INSTALL "${LSL_DIR}/builtins.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/lsl_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/lua_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/secondlife.d.luau" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/secondlife.docs.json" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/secondlife_selene.yml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")

vcpkg_install_copyright(FILE_LIST "${LSL_DIR}/LICENSE")
