# -*- cmake -*-

project(llxml)

include(00-Common)
include(LLCommon)

set(llxml_SOURCE_FILES
    llcontrol.cpp
    llxmlnode.cpp
    llxmlparser.cpp
    llxmltree.cpp
    )

set(llxml_HEADER_FILES
    CMakeLists.txt

    llcontrol.h
    llxmlnode.h
    llxmlparser.h
    llxmltree.h
    )

list(APPEND llxml_SOURCE_FILES ${llxml_HEADER_FILES})

add_library (llxml ${llxml_SOURCE_FILES})
# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
target_link_libraries( llxml
        llfilesystem
        llmath
        llcommon
        ll::expat
    )
target_include_directories( llxml  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})


if(USE_PRECOMPILED_HEADERS AND ${CMAKE_VERSION} VERSION_GREATER "3.15.0") 
  target_precompile_headers(llxml
    PRIVATE
    [["linden_common.h"]]
    )
endif()

# tests

if (LL_TESTS)
    # unit tests

    SET(llxml_TEST_SOURCE_FILES
      # none yet!
      )
    LL_ADD_PROJECT_UNIT_TESTS(llxml "${llxml_TEST_SOURCE_FILES}")

    # integration tests

    #    set(TEST_DEBUG on)
    set(test_libs
            llxml
            llmath
            llcommon
            )

    LL_ADD_INTEGRATION_TEST(llcontrol "" "${test_libs}")
endif (LL_TESTS)
