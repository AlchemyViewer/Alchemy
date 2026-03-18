set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(
    LSL_ARCHIVE
    URLS "https://github.com/secondlife/lsl-definitions/releases/download/v${VERSION}/lsl-definitions.zip"
    FILENAME lsl-definitions.zip
    SHA512 a5344cc7a945d1208b0a1e27dc5cbe99a2dd28d9836f79e5a760d5df074b0393590a335f743209b7d5dec00fa6e7017aa2dfe7f16b3bf3f5df19cc511eca2958
)

vcpkg_extract_source_archive(LSL_DIR ARCHIVE ${LSL_ARCHIVE} NO_REMOVE_ONE_LEVEL)

file(INSTALL "${LSL_DIR}/builtins.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/lsl_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_default.d.luau" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_default.docs.json" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/slua_selene.yml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")

file(TOUCH "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright")
