project(SLPlugin)

include(00-Common)
include(LLCommon)
include(LLPlugin)
include(Linking)
include(PluginAPI)
include(LLMessage)

include_directories(
    ${LLPLUGIN_INCLUDE_DIRS}
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
)
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    )

if (DARWIN)
    include(CMakeFindFrameworks)
    find_library(COCOA_LIBRARY Cocoa)
endif (DARWIN)


### SLPlugin

set(SLPlugin_SOURCE_FILES
    slplugin.cpp
    )

if (DARWIN)
  list(APPEND SLPlugin_SOURCE_FILES
    slplugin-objc.mm
  )
  list(APPEND SLPlugin_HEADER_FILES
    slplugin-objc.h
  )
endif (DARWIN)

set_source_files_properties(${SLPlugin_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

if (SLPlugin_HEADER_FILES)
  list(APPEND SLPlugin_SOURCE_FILES ${SLPlugin_HEADER_FILES})
endif (SLPlugin_HEADER_FILES)

add_executable(SLPlugin
    WIN32
    MACOSX_BUNDLE
    ${SLPlugin_SOURCE_FILES}
    )

target_link_libraries(SLPlugin
  ${LLPLUGIN_LIBRARIES}
  ${LLMESSAGE_LIBRARIES}
  ${LLCOMMON_LIBRARIES}
  ${PLUGIN_API_WINDOWS_LIBRARIES}
)

if (WINDOWS)
  target_link_libraries(SLPlugin wer)
  set_target_properties(SLPlugin
    PROPERTIES
    LINK_FLAGS_DEBUG "/NODEFAULTLIB:\"LIBCMTD\""
    )
elseif (LINUX)
  set_target_properties(SLPlugin 
    PROPERTIES
    LINK_FLAGS "-Wl,--disable-new-dtags"
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/llplugin")
elseif (DARWIN)
  set_target_properties(SLPlugin
    PROPERTIES
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@executable_path/../../../../Frameworks;@executable_path/../Frameworks;@executable_path/../Frameworks/plugins"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/slplugin_info.plist
    )

  # Mac version needs to link against Cocoa
  target_link_libraries(SLPlugin ${COCOA_LIBRARY})
  # Make sure the app bundle has a Resources directory (it will get populated by viewer-manifest.py later)
  add_custom_command(
    TARGET SLPlugin POST_BUILD
    COMMAND mkdir
    ARGS
      -p
      ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/SLPlugin.app/Contents/Resources
  )
endif ()
