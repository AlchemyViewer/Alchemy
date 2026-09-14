# -*- cmake -*-

set(
  appearance_utility_SOURCES
  appearance_utility.cpp
  llappappearanceutility.cpp
  llbakingavatar.cpp
  llbakingjoint.cpp
  llbakingjointmesh.cpp
  llbakingprocess.cpp
  llbakingshadermgr.cpp
  llbakingtexlayer.cpp
  llbakingtexture.cpp
  llbakingwearable.cpp
  llbakingwearablesdata.cpp
  llbakingwindow.cpp
  llprocessparams.cpp
  llprocessskin.cpp
  llprocesstexture.cpp
)

set(
  appearance_utility_HEADERS
  llappappearanceutility.h
  llbakingavatar.h
  llbakingjoint.h
  llbakingjointmesh.h
  llbakingprocess.h
  llbakingshadermgr.h
  llbakingtexlayer.h
  llbakingtexture.h
  llbakingwearable.h
  llbakingwearablesdata.h
  llbakingwindow.h
  llprocessparams.h
  llprocessskin.h
  llprocesstexture.h
)

add_executable(appearance-utility-bin)

target_sources(
  appearance-utility-bin
  PRIVATE ${appearance_utility_SOURCES} ${appearance_utility_HEADERS}
)

target_link_libraries(
  appearance-utility-bin
  PRIVATE al::flags llcharacter llinventory llappearance llwindow llrender llxml
)

target_precompile_headers(appearance-utility-bin REUSE_FROM llprecompiled_exe)

# What the utility reads at run time, beside the executable as the viewer
# has it: the avatar definitions and the baking shaders, from the viewer's
# own source tree.
add_custom_command(
  TARGET appearance-utility-bin
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory_if_different ${INDRA_SOURCE_DIR}/newview/character
    $<TARGET_FILE_DIR:appearance-utility-bin>/character
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory_if_different ${INDRA_SOURCE_DIR}/newview/app_settings/shaders
    $<TARGET_FILE_DIR:appearance-utility-bin>/app_settings/shaders
  COMMENT "Staging the appearance utility's data beside it"
  VERBATIM
)
