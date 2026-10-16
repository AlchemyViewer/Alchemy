# -*- cmake -*-

add_library(llxml STATIC)

target_sources(
  llxml
  PRIVATE
    alxmldocument.cpp
    alxmllayermerge.cpp
    alxmlmergelog.cpp
    llcontrol.cpp
    llxmlnode.cpp
    llxmltree.cpp
)

target_sources(
  llxml
  PUBLIC
    FILE_SET HEADERS
      FILES alxmldocument.h alxmllayermerge.h alxmlmergelog.h llcontrol.h llxmlnode.h llxmltree.h
)

target_link_libraries(llxml PRIVATE al::flags PUBLIC llfilesystem llmath llcommon ll::pugixml)
target_precompile_headers(llxml REUSE_FROM llprecompiled)

# tests
if(AL_BUILD_TESTS)
  # The state guard's checks on what llxml keeps for the process, for every
  # test binary that links it.
  add_library(llxml_test_checks OBJECT tests/llxml_test_checks.cpp)
  target_link_libraries(llxml_test_checks PRIVATE al::flags al_test_support llxml)
  set_target_properties(llxml_test_checks PROPERTIES FOLDER "Tests/llxml")

  al_add_test_binary(
    llxml_tests
    PROJECT llxml
    SOURCES tests/alxmllayermerge_test.cpp tests/llcontrol_test.cpp tests/llxmlnode_test.cpp
    LIBRARIES llxml llmath llcommon
    SUPPORT llxml_test_checks
  )
endif()
