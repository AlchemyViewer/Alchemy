# -*- cmake -*-

include(PluginAPI)

### media_plugin_example - dedicated host executable that statically links the
### example media plugin together with the slplugin host driver (no dlopen).

set(media_plugin_example_SOURCE_FILES
    media_plugin_example.cpp
    )

add_executable(media_plugin_example
    WIN32
    MACOSX_BUNDLE
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin.cpp
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin_static.cpp
    ${media_plugin_example_SOURCE_FILES}
    )

if (DARWIN)
    target_sources(media_plugin_example PRIVATE ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin-objc.mm)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin-objc.mm
        PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif ()

target_link_libraries(media_plugin_example
        media_plugin_base
        llplugin
        llmessage
        llcommon
        ll::pluginlibraries
)

if (WINDOWS)
  set_target_properties(media_plugin_example
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${VIEWER_STAGING_DIR}/llplugin"
    )
  target_link_options(media_plugin_example PRIVATE /MANIFEST:NO)
elseif (DARWIN)
  set_target_properties(media_plugin_example
    PROPERTIES
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@executable_path/../../../../Frameworks;@executable_path/../Frameworks;@executable_path/../Frameworks/plugins"
  )
endif ()
