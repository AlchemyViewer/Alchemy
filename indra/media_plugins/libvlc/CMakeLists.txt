# -*- cmake -*-

include(PluginAPI)
include(LibVLCPlugin)

### media_plugin_libvlc - dedicated host executable that statically links the
### LibVLC media plugin together with the slplugin host driver (there is no
### dlopen'd plugin library any more).

set(media_plugin_libvlc_SOURCE_FILES
    media_plugin_libvlc.cpp
    )

add_executable(media_plugin_libvlc
    WIN32
    MACOSX_BUNDLE
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin.cpp
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin_static.cpp
    ${media_plugin_libvlc_SOURCE_FILES}
    )

if (DARWIN)
    target_sources(media_plugin_libvlc PRIVATE ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin-objc.mm)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin-objc.mm
        PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif ()

target_link_libraries(media_plugin_libvlc
        media_plugin_base
        ll::libvlc
        llplugin
        llmessage
        llcommon
        ll::pluginlibraries
)

if (WINDOWS)
  set_target_properties(media_plugin_libvlc
    PROPERTIES
    EXCLUDE_FROM_DEFAULT_BUILD_DEBUG ON
    RUNTIME_OUTPUT_DIRECTORY "${VIEWER_STAGING_DIR}/llplugin"
    )
  target_link_options(media_plugin_libvlc PRIVATE /MANIFEST:NO)

  # Copy VLC runtime plugins to the packaging directory
  add_custom_command(
    TARGET media_plugin_libvlc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/plugins/vlc-bin" "${VIEWER_STAGING_DIR}/llplugin/plugins"
  )
elseif (DARWIN)
  set_target_properties(media_plugin_libvlc
    PROPERTIES
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@executable_path/../../../../Frameworks;@executable_path/../Frameworks;@executable_path/../Frameworks/plugins"
  )
endif ()
