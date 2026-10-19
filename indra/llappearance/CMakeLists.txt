# -*- cmake -*-

set(
  llappearance_SOURCES
  llavatarappearance.cpp
  llavatarjoint.cpp
  llavatarjointmesh.cpp
  lldriverparam.cpp
  lllocaltextureobject.cpp
  llpolyskeletaldistortion.cpp
  llpolymesh.cpp
  llpolymorph.cpp
  lltexglobalcolor.cpp
  lltexlayer.cpp
  lltexlayerparams.cpp
  llwearable.cpp
  llwearabledata.cpp
  llwearabletype.cpp
  llviewervisualparam.cpp
  llavatarappearancedefines.cpp
)

set(
  llappearance_HEADERS
  llavatarappearance.h
  llavatarjoint.h
  llavatarjointmesh.h
  lldriverparam.h
  lljointdata.h
  lljointpickname.h
  lllocaltextureobject.h
  llpolyskeletaldistortion.h
  llpolymesh.h
  llpolymorph.h
  lltexglobalcolor.h
  lltexlayer.h
  lltexlayerparams.h
  llwearable.h
  llwearabledata.h
  llwearabletype.h
  llviewervisualparam.h
  llavatarappearancedefines.h
)

add_library(llappearance STATIC)

target_sources(
  llappearance
  PRIVATE ${llappearance_SOURCES}
  PUBLIC FILE_SET HEADERS FILES ${llappearance_HEADERS}
)

target_link_libraries(
  llappearance
  PRIVATE al::flags
  PUBLIC
    llcharacter
    llinventory
    llimage
    llrender
    llfilesystem
    llmath
    llxml
    llmessage
    llcorehttp
    llcommon
)
# The texture layers draw with llui's helpers (llui.h), whose headers name
# Alchemy's from llui's folders: those on the path, though nothing of llui
# is linked here.
target_include_directories(
  llappearance
  PRIVATE $<TARGET_PROPERTY:llui,INTERFACE_INCLUDE_DIRECTORIES>
)
target_precompile_headers(llappearance REUSE_FROM llprecompiled)

if(AL_BUILD_TESTS)
  al_add_test_binary(
    llappearance_tests
    PROJECT llappearance
    SOURCES tests/llavatarappearancedefines_test.cpp tests/llwearabletype_test.cpp
    # The dictionary lookups LLVOAvatar::updateTextures makes for every texture entry.
    BENCH_SOURCES tests/llavatarappearancedefines_bench.cpp
    LIBRARIES llappearance
  )

  # What llappearance asks the viewer for, defined once.
  add_library(llappearance_test_fakes OBJECT tests/llappearance_test_fakes.cpp)
  target_link_libraries(llappearance_test_fakes PRIVATE al::flags llrender)
  set_target_properties(llappearance_test_fakes PROPERTIES FOLDER "Tests/llappearance")

  # The texture layers' morph mask readback on the hidden window, in the one
  # context the process shares.
  al_add_test_binary(
    llappearance_gl_tests
    PROJECT llappearance
    SOURCES tests/lltexlayer_gl_test.cpp
    LIBRARIES llappearance
    SUPPORT llappearance_test_fakes llrender_test_checks llcommon_test_checks
    GL
  )
endif()
