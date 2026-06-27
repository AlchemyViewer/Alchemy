# FAudio uses calender versioning (e.g., 26.01), but vcpkg drops them in versions
string(REGEX REPLACE "^([0-9]+)\\.([1-9])$" "\\1.0\\2" FAUDIO_REF "${VERSION}")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FNA-XNA/faudio
    REF "${FAUDIO_REF}"
    SHA512 e2efecf88bf62232da7b3ac04e21c19ae801ebe05c185c4b1775e2deae7dc9d95de7140ef5759439ca94b6cb4cf3a03cc08cf84c781d0d8a906964ee36876cbc
    HEAD_REF master
    PATCHES
        # Apple's arm64 linker (Xcode 26+) rejects merged globals where a
        # packed-struct pointer field lands at a non-8-byte-aligned offset.
        # F3DAUDIO_DISTANCE_CURVE is pack(1) with `pPoints` as its first
        # field; force the static curve instances in F3DAudio.c / FACT3D.c
        # to pointer alignment so the link succeeds.
        apple-arm64-curve-alignment.patch
)

set(options "")
if(VCPKG_TARGET_IS_WINDOWS)
    list(APPEND options -DPLATFORM_WIN32=TRUE)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${options}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/FAudio)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(
    COMMENT "FAudio is licensed under the Zlib license."
    FILE_LIST
       "${SOURCE_PATH}/LICENSE"
)
