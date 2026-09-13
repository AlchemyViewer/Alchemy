# -*- cmake -*-
#
# The build system's own files, listed so they show in the IDE. Nothing is
# built; a new module joins the list on the next configure.

file(GLOB cmake_MODULES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cmake)
file(GLOB cmake_TRIPLETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/triplets/*.cmake)

source_group("Shared Rules" FILES ${cmake_MODULES})
source_group("Triplets" FILES ${cmake_TRIPLETS})
source_group("CMake Rules" FILES CMakeLists.txt)
source_group(
  "Master Rules"
  FILES
    ${CMAKE_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/CMakePresets.json
    ${CMAKE_SOURCE_DIR}/vcpkg.json
    ${CMAKE_SOURCE_DIR}/vcpkg-configuration.json
)

add_custom_target(
  cmake_rules
  SOURCES
    ${cmake_MODULES}
    ${cmake_TRIPLETS}
    CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/CMakePresets.json
    ${CMAKE_SOURCE_DIR}/vcpkg.json
    ${CMAKE_SOURCE_DIR}/vcpkg-configuration.json
)
