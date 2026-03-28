set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(
    LSL_ARCHIVE
    URLS "https://github.com/secondlife/lsl-definitions/releases/download/v${VERSION}/lsl-definitions.zip"
    FILENAME lsl-definitions.zip
    SHA512 56e683ba340b238f535739001718c83f0235a9fc2ce263db521af021c788743e5b880056a95fa7c38ba2a9440a59c2968f286db7d240cd8e84c6bbd91a4937a5
)

vcpkg_extract_source_archive(LSL_DIR ARCHIVE ${LSL_ARCHIVE} NO_REMOVE_ONE_LEVEL)

file(INSTALL "${LSL_DIR}/builtins.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/lsl_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_default.d.luau" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_default.docs.json" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_selene.yml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")

vcpkg_install_copyright(FILE_LIST "${LSL_DIR}/LICENSE")
