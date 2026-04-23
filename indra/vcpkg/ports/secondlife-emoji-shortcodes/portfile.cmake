vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO milesj/emojibase
    REF emojibase-data@${VERSION}
    SHA512 4b57ec44a1706f7a137cc06da744a03ec563f7fc3c6fc482cca7dccfd9ce89cd08bb26526ddaf753aaebb7c5cea8eec841752321ade0cd8fdfe5ad0caecc2ca4
    HEAD_REF master
)

vcpkg_find_acquire_program(PYTHON3)

vcpkg_execute_required_process(
    COMMAND ${PYTHON3} "${CURRENT_PORT_DIR}/gen_emoji_characters.py" "${SOURCE_PATH}/packages/data" "${CURRENT_PACKAGES_DIR}/share/${PORT}/xui"
    WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
    LOGNAME "build-${TARGET_TRIPLET}-dbg")

vcpkg_install_copyright(FILE_LIST ${SOURCE_PATH}/LICENSE)
