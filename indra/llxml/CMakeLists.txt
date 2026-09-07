# -*- cmake -*-

add_library(llxml STATIC)

target_sources(llxml
    PRIVATE
    alxmldocument.cpp
    alxmllayermerge.cpp
    alxmlmergelog.cpp
    llcontrol.cpp
    llxmlnode.cpp
    llxmltree.cpp
    )

target_sources(llxml
    PUBLIC
    FILE_SET HEADERS
    FILES
    alxmldocument.h
    alxmllayermerge.h
    alxmlmergelog.h
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
    LL_ADD_INTEGRATION_TEST(alxmllayermerge "" "${test_libs}" "${test_project}")

    # The overlay census: the viewer's own merge over every skin and
    # language of the source tree, gated by the committed baseline.
    add_executable(xui_overlay_check tests/xui_overlay_check.cpp)
    set_target_properties(xui_overlay_check
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
        FOLDER "Tests/llxml"
        )
    target_link_libraries(xui_overlay_check ${test_libs})
    add_dependencies(BUILD_TESTS xui_overlay_check)
    add_test(NAME xui_overlay_check
        COMMAND xui_overlay_check "${INDRA_SOURCE_DIR}/newview/skins" "${CMAKE_CURRENT_SOURCE_DIR}/tests/xui_overlay_baseline.txt" --examples 0)
endif ()
