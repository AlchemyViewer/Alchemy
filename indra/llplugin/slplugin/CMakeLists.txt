project(ALPlugin)

include(00-Common)
include(LLCommon)
include(Linking)
include(PluginAPI)

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

if (SLPlugin_HEADER_FILES)
  list(APPEND SLPlugin_SOURCE_FILES ${SLPlugin_HEADER_FILES})
endif (SLPlugin_HEADER_FILES)

add_executable(ALPlugin
    WIN32
    MACOSX_BUNDLE
    ${SLPlugin_SOURCE_FILES}
    )

target_link_libraries(ALPlugin
        PUBLIC
        llplugin
        llmessage
        llcommon
        ll::pluginlibraries
        )

if (WINDOWS)
  set_target_properties(ALPlugin
    PROPERTIES
    LINK_FLAGS_DEBUG "/NODEFAULTLIB:\"LIBCMTD\""
    )
elseif (LINUX)
  set_target_properties(ALPlugin 
    PROPERTIES
    LINK_FLAGS "-Wl,--disable-new-dtags"
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/llplugin")
elseif (DARWIN)
  set_target_properties(ALPlugin
    PROPERTIES
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@executable_path/../../../../Frameworks;@executable_path/../Frameworks;@executable_path/../Frameworks/plugins"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/slplugin_info.plist
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.alchemyviewer.viewer.slplugin"
    )

  # Make sure the app bundle has a Resources directory (it will get populated by viewer-manifest.py later)
  add_custom_command(
    TARGET ALPlugin POST_BUILD
    COMMAND mkdir
    ARGS
      -p
      ${CMAKE_CURRENT_BINARY_DIR}/$<IF:$<BOOL:${LL_GENERATOR_IS_MULTI_CONFIG}>,$<CONFIG>,>/ALPlugin.app/Contents/Resources
  )
endif ()
