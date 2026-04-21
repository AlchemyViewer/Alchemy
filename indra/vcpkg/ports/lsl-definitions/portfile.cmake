set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(
    LSL_ARCHIVE
    URLS "https://github.com/secondlife/lsl-definitions/releases/download/v${VERSION}/lsl-definitions.zip"
    FILENAME lsl-definitions.${VERSION}.zip
    SHA512 a12225cecb51ab0cb708a5bd0e80d85f43f086d3d1bf80c9166d014c4f207a783f792cad7db83da3f6d8b7478e9e07a15e45f32b45c5c2eb4069f4a0fbba89b9
)

vcpkg_extract_source_archive(LSL_DIR ARCHIVE ${LSL_ARCHIVE} NO_REMOVE_ONE_LEVEL)

file(INSTALL "${LSL_DIR}/builtins.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/lsl_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/lua_keywords_pretty.xml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/secondlife.d.luau" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/secondlife.docs.json" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")
file(INSTALL "${LSL_DIR}/secondlife_selene.yml" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/definitions")

vcpkg_install_copyright(FILE_LIST "${LSL_DIR}/LICENSE")
