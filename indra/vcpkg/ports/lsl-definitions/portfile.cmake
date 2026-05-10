set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(
    LSL_ARCHIVE
    URLS "https://github.com/secondlife/lsl-definitions/releases/download/v${VERSION}/lsl_definitions-${VERSION}-common-25461772657.tar.zst"
    FILENAME lsl-definitions.${VERSION}.tar.zst
    SHA512 80bacb56ddebf8816ce6a8f30e9d1deedaba48e68bc2a71e5ca2ffea2ee9edcbfca8dc2c24bb97d982634b5dd6da982e12af25515e5002da941746212ebcfd95
)

vcpkg_extract_source_archive(LSL_DIR ARCHIVE ${LSL_ARCHIVE} NO_REMOVE_ONE_LEVEL)

file(INSTALL "${LSL_DIR}/lsl_definitions/builtins.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/lsl_definitions.yaml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/lsl_keywords.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/lua_keywords.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/secondlife.d.luau" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/secondlife.docs.json" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/secondlife_selene.yml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")
file(INSTALL "${LSL_DIR}/lsl_definitions/slua_definitions.yaml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/lsl_definitions")

vcpkg_install_copyright(FILE_LIST "${LSL_DIR}/LICENSES/lsl_definitions.txt")
