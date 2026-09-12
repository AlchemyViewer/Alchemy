# -*- cmake -*-

add_library(lllogin STATIC)

target_sources(lllogin
    PRIVATE
    lllogin.cpp
    PUBLIC
    FILE_SET HEADERS
    FILES
    lllogin.h
    )

target_link_libraries(lllogin
        PRIVATE
        al::flags
        PUBLIC
        llmessage
        llcorehttp
        llcommon
        llmath
        llxml
    )

if(AL_BUILD_TESTS)
  SET(lllogin_TEST_SOURCE_FILES
      lllogin.cpp
      )
  set_source_files_properties(
    lllogin.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES llmessage llcorehttp llcommon
    )

  LL_ADD_PROJECT_UNIT_TESTS(lllogin "${lllogin_TEST_SOURCE_FILES}")
endif()
