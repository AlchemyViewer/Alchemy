# -*- cmake -*-

# add a target to generate API documentation with Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
  get_filename_component(DOXYGEN_TOP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. PATH)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(
    doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
endif()
