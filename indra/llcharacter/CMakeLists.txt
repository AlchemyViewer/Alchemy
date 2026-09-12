# -*- cmake -*-

add_library(llcharacter STATIC)

target_sources(llcharacter
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

target_sources(llcharacter
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
    PRIVATE
    al::flags
    PUBLIC
    llcommon
    llmath
    llmessage
    llfilesystem
    llxml
    )

target_precompile_headers(llcharacter REUSE_FROM llprecompiled)

if (AL_BUILD_TESTS)
  al_add_test(lljoint PROJECT llcharacter SOURCES lljoint.cpp LIBRARIES llcommon llmath)

  foreach(test llpose llcharacter llmotioncontroller llkeyframemotion llmultigesture)
    al_add_test(${test} PROJECT llcharacter LIBRARIES llcharacter)
  endforeach()

  # The translation table this parser reads is shipped by newview, and one it
  # refuses stops every BVH upload, so the test reads the shipped file itself.
  # The joint names a .bvh may use come from the shipped skeleton, and the
  # root joint check will not pass without them, so the test reads it too.
  al_add_test(llbvhloader PROJECT llcharacter LIBRARIES llcharacter
    DEFINES
    "AL_SHIPPED_ANIM_INI=\"${CMAKE_CURRENT_SOURCE_DIR}/../newview/app_settings/anim.ini\""
    "AL_SHIPPED_AVATAR_SKELETON=\"${CMAKE_CURRENT_SOURCE_DIR}/../newview/character/avatar_skeleton.xml\"")
endif()
