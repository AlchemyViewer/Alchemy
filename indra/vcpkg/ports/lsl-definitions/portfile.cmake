set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(
    LSL_ARCHIVE
    URLS "https://github.com/secondlife/lsl-definitions/releases/download/v${VERSION}/lsl_definitions-${VERSION}-common-29281156609.tar.zst"
    FILENAME lsl-definitions.${VERSION}.tar.zst
    SHA512 81e0784e20f7b21a8e23cd0dc4c64e19a8306a03826b70bbeade133b764e6247ee59d66845e8d0515ea6740f64f5fe0f3b1f2f45f87e620a0b47e214f2da762a
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
