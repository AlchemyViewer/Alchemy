# -*- cmake -*-

add_library(llxml STATIC)

target_sources(llxml
    PRIVATE
    alxmldocument.cpp
    llcontrol.cpp
    llxmlnode.cpp
    llxmltree.cpp
    )

target_sources(llxml
    PUBLIC
    FILE_SET HEADERS
    FILES
    alxmldocument.h
    llcontrol.h
    llxmlnode.h
    llxmltree.h
    )

# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
target_link_libraries(llxml
    PUBLIC
    llfilesystem
    llmath
    llcommon
    ll::pugixml
    )
target_include_directories( llxml  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(llxml REUSE_FROM llprecompiled)

# tests
if (BUILD_TESTING)
    # unit tests
    SET(llxml_TEST_SOURCE_FILES
      llxmlnode.cpp
      )
    set_source_files_properties(llxmlnode.cpp
      PROPERTIES
      LL_TEST_ADDITIONAL_SOURCE_FILES alxmldocument.cpp
      LL_TEST_ADDITIONAL_LIBRARIES "ll::pugixml"
      )
    LL_ADD_PROJECT_UNIT_TESTS(llxml "${llxml_TEST_SOURCE_FILES}")

    # integration tests

    # set(TEST_DEBUG on)
    set(test_libs
            llxml
            llmath
            llcommon
            )
    set(test_project llxml)

    LL_ADD_INTEGRATION_TEST(llcontrol "" "${test_libs}" "${test_project}")
endif ()
