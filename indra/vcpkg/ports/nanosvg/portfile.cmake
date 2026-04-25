if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO memononen/nanosvg
    REF 5cefd9847949af6df13f65027fd43af5a7513633
    SHA512 6135e3b5cd71faca795e27afc6c264605cdec13cf34be9d20f20954d7a52f06982ce8562679610a27b215437250b6909f0496ee34963360cd26f7c389bf55b27
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/NanoSVG)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
