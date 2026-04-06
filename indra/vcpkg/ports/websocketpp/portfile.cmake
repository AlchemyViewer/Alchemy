#header-only library

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO zaphoyd/websocketpp
    REF 1b11fd301531e6df35a6107c1e8665b1e77a2d8e # master/0.8.2
    SHA512 7ff63bac4c00a34d4db92dd0318dc38981f7ee2f45fcc24d6bde4baac2e099e90d96b26ec2c9f958d2ea44845b2cbcff02a2e376d0ee10975b02b7d26fc3d3bc
    HEAD_REF master
    PATCHES
    cpp20.patch
    cpp23.patch
    boost187.patch
)

file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/share/${PORT})

# Copy the header files
file(COPY "${SOURCE_PATH}/websocketpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include" FILES_MATCHING PATTERN "*.hpp")

set(PACKAGE_INSTALL_INCLUDE_DIR "\${CMAKE_CURRENT_LIST_DIR}/../../include")
set(WEBSOCKETPP_VERSION 0.8.2)
set(PACKAGE_INIT "
macro(set_and_check)
  set(\${ARGV})
endmacro()
")
configure_file(${SOURCE_PATH}/websocketpp-config.cmake.in "${CURRENT_PACKAGES_DIR}/share/${PORT}/websocketpp-config.cmake" @ONLY)
configure_file(${SOURCE_PATH}/COPYING ${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright COPYONLY)
