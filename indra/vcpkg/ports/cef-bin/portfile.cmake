set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_FIXUP_MACHO_RPATH OFF)
set(VCPKG_FIXUP_ELF_RPATH OFF)
set(VCPKG_LIBRARY_LINKAGE static)

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://cef-builds.spotifycdn.com/cef_binary_149.0.6%2Bg0d0eeb6%2Bchromium-149.0.7827.201_windows64.tar.bz2"
        FILENAME "cef.${VERSION}.windows64.tar.bz2"
        SHA512 66200e0050721e2d5df68fcd737daa48080ae616cc063d0cc452d0059ae9e355b5c50182fe532120ba19d92a91559db1a8b68f7ae51d5e281f189474bdf8da52
    )
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
        set(MACOS_ARCH_FLAG "-DPROJECT_ARCH=arm64")
        vcpkg_download_distfile(ARCHIVE
            URLS "https://cef-builds.spotifycdn.com/cef_binary_149.0.6%2Bg0d0eeb6%2Bchromium-149.0.7827.201_macosarm64.tar.bz2"
            FILENAME "cef.${VERSION}.macosarm64.tar.bz2"
            SHA512 93a547aa37226c4a7715a0b142249f1e654d97f21c2e6b63bf78e2d3c6ec6f7f3ea35bc34e319125972ae48670c78dfc636b996881a11e1dc8b34110bf9b29b0
        )
    else()
        set(MACOS_ARCH_FLAG "-DPROJECT_ARCH=x86_64")
        vcpkg_download_distfile(ARCHIVE
            URLS "https://cef-builds.spotifycdn.com/cef_binary_149.0.6%2Bg0d0eeb6%2Bchromium-149.0.7827.201_macosx64.tar.bz2"
            FILENAME "cef.${VERSION}.macosx64.tar.bz2"
            SHA512 dcd06bdf58c19731f2eeedabc92ce8476510aea3074418d57f1b1c7a8e5753b848ca4bf5744906cca9ee3685af4f8c6b081d0c8350bb2a87b1366cf80b46f4a1
        )
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://cef-builds.spotifycdn.com/cef_binary_149.0.6%2Bg0d0eeb6%2Bchromium-149.0.7827.201_linux64.tar.bz2"
        FILENAME "cef.${VERSION}.linux64.tar.bz2"
        SHA512 dab6871d532675cf63bf6935ba95fc9f8364eecbf3627ea28acf4a40b09cce466e40dd43095f4be89b7094a9c75311cac65bcfc56d30bde381308783644d6d5c
    )
endif()

vcpkg_extract_source_archive(
    CEF_SOURCE_PATH
    ARCHIVE ${ARCHIVE}
)

if(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_CRT_LINKAGE MATCHES "dynamic")
        vcpkg_cmake_configure(
            SOURCE_PATH ${CEF_SOURCE_PATH}
            OPTIONS
                -DCEF_RUNTIME_LIBRARY_FLAG="/MD"
        )
    else()
        vcpkg_cmake_configure(
            SOURCE_PATH ${CEF_SOURCE_PATH}
        )
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    vcpkg_cmake_configure(
        SOURCE_PATH ${CEF_SOURCE_PATH}
        OPTIONS
            ${MACOS_ARCH_FLAG}
    )
else()
    vcpkg_cmake_configure(
        SOURCE_PATH ${CEF_SOURCE_PATH}
    )
endif()

vcpkg_cmake_build(
    TARGET libcef_dll_wrapper
)
vcpkg_copy_pdbs()

file(INSTALL "${CEF_SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/cef/include")

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libcef_dll_wrapper/libcef_dll_wrapper.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    if(NOT VCPKG_BUILD_TYPE)
        file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/libcef_dll_wrapper/libcef_dll_wrapper.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    endif()
else()
    file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libcef_dll_wrapper/libcef_dll_wrapper.a" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    if(NOT VCPKG_BUILD_TYPE)
        file(INSTALL "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/libcef_dll_wrapper/libcef_dll_wrapper.a" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    endif()
endif()

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${CEF_SOURCE_PATH}/Release/libcef.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL "${CEF_SOURCE_PATH}/Release/libcef.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL
        DIRECTORY "${CEF_SOURCE_PATH}/Release/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Release"
        FILES_MATCHING
        PATTERN "*.*"
        PATTERN "libcef.dll" EXCLUDE
        PATTERN "libcef.lib" EXCLUDE
    )
    if(NOT VCPKG_BUILD_TYPE)
        file(INSTALL "${CEF_SOURCE_PATH}/Debug/libcef.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
        file(INSTALL "${CEF_SOURCE_PATH}/Debug/libcef.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
        file(INSTALL
            DIRECTORY "${CEF_SOURCE_PATH}/Debug/"
            DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Debug"
            FILES_MATCHING
            PATTERN "*.*"
            PATTERN "libcef.dll" EXCLUDE
            PATTERN "libcef.lib" EXCLUDE
        )
    endif()

    file(INSTALL "${CEF_SOURCE_PATH}/Resources" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
elseif(VCPKG_TARGET_IS_OSX)
    set(CEF_RELEASE_FRAMEWORK_DIR "${CURRENT_PACKAGES_DIR}/lib/Chromium Embedded Framework.framework")
    file(RENAME "${CEF_SOURCE_PATH}/Release/Chromium Embedded Framework.framework" "${CEF_RELEASE_FRAMEWORK_DIR}")
    if(NOT VCPKG_BUILD_TYPE)
        set(CEF_RELEASE_FRAMEWORK_DIR "${CURRENT_PACKAGES_DIR}/debug/lib/Chromium Embedded Framework.framework")
        file(RENAME "${CEF_SOURCE_PATH}/Debug/Chromium Embedded Framework.framework" "${CEF_RELEASE_FRAMEWORK_DIR}")
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    file(INSTALL "${CEF_SOURCE_PATH}/Release/libcef.so" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL
        DIRECTORY "${CEF_SOURCE_PATH}/Release/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Release"
        FILES_MATCHING
        PATTERN "*.*"
        PATTERN "chrome-sandbox"
        PATTERN "libcef.so" EXCLUDE
    )
    if(NOT VCPKG_BUILD_TYPE)
        file(INSTALL "${CEF_SOURCE_PATH}/Debug/libcef.so" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
        file(INSTALL
            DIRECTORY "${CEF_SOURCE_PATH}/Debug/"
            DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Debug"
            FILES_MATCHING
            PATTERN "*.*"
            PATTERN "chrome-sandbox"
            PATTERN "libcef.so" EXCLUDE
        )
    endif()

    file(INSTALL "${CEF_SOURCE_PATH}/Resources" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
endif()

file(INSTALL "${CEF_SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
