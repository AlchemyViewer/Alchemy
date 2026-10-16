# -*- cmake -*-

add_library(llcharacter STATIC)

target_sources(
  llcharacter
  PRIVATE
    llanimationstates.cpp
    llbvhloader.cpp
    llcharacter.cpp
    lleditingmotion.cpp
    llgesture.cpp
    llhandmotion.cpp
    llheadrotmotion.cpp
    lljoint.cpp
    lljointsolverrp3.cpp
    llkeyframefallmotion.cpp
    llkeyframemotion.cpp
    llkeyframestandmotion.cpp
    llkeyframewalkmotion.cpp
    llmotioncontroller.cpp
    llmotion.cpp
    llmultigesture.cpp
    llpose.cpp
    lltargetingmotion.cpp
    llvisualparam.cpp
)

target_sources(
  llcharacter
  PUBLIC
    FILE_SET HEADERS
      FILES
        llanimationstates.h
        llbvhloader.h
        llbvhconsts.h
        llcharacter.h
        lleditingmotion.h
        llgesture.h
        llhandmotion.h
        llheadrotmotion.h
        lljoint.h
        lljointsolverrp3.h
        lljointstate.h
        llkeyframefallmotion.h
        llkeyframemotion.h
        llkeyframestandmotion.h
        llkeyframewalkmotion.h
        llmotion.h
        llmotioncontroller.h
        llmultigesture.h
        llpose.h
        lltargetingmotion.h
        llvisualparam.h
)

target_link_libraries(
  llcharacter
  PRIVATE al::flags
  PUBLIC llcommon llmath llmessage llfilesystem llxml
)

target_precompile_headers(llcharacter REUSE_FROM llprecompiled)

if(AL_BUILD_TESTS)
  # The state guard's checks on what llcharacter keeps for the process, for
  # every test binary that links it.
  add_library(llcharacter_test_checks OBJECT tests/llcharacter_test_checks.cpp)
  target_link_libraries(llcharacter_test_checks PRIVATE al::flags al_test_support llcharacter)
  set_target_properties(llcharacter_test_checks PROPERTIES FOLDER "Tests/llcharacter")

  # The translation table the BVH parser reads is shipped by newview, and one
  # it refuses stops every BVH upload, so llbvhloader's tests read the shipped
  # file itself. The joint names a .bvh may use come from the shipped
  # skeleton, and the root joint check will not pass without them, so the
  # tests read it too.
  al_add_test_binary(
    llcharacter_tests
    PROJECT llcharacter
    SOURCES
      tests/llbvhloader_test.cpp
      tests/llcharacter_test.cpp
      tests/lljoint_test.cpp
      tests/llkeyframemotion_test.cpp
      tests/llmotioncontroller_test.cpp
      tests/llmultigesture_test.cpp
      tests/llpose_test.cpp
    LIBRARIES llcharacter
    SUPPORT llcharacter_test_checks
    DEFINES
      "AL_SHIPPED_ANIM_INI=\"${CMAKE_CURRENT_SOURCE_DIR}/../newview/app_settings/anim.ini\""
      "AL_SHIPPED_AVATAR_SKELETON=\"${CMAKE_CURRENT_SOURCE_DIR}/../newview/character/avatar_skeleton.xml\""
  )
endif()
