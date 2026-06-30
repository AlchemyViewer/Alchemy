# -*- cmake -*-

include(PluginAPI)
include(GLIB)

include(GStreamer10Plugin)

### media_plugin_gstreamer10 - dedicated host executable that statically links the
### GStreamer media plugin together with the slplugin host driver (no dlopen). This
### plugin is only built on UNIX-but-not-Apple (see BUILD_GSTREAMER_PLUGIN).

set(media_plugin_gstreamer10_SOURCE_FILES
    media_plugin_gstreamer10.cpp
    )

set(media_plugin_gstreamer10_HEADER_FILES
    llmediaimplgstreamer_syms.h
    )

add_executable(media_plugin_gstreamer10
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin.cpp
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin_static.cpp
    ${media_plugin_gstreamer10_SOURCE_FILES}
)

target_link_libraries(media_plugin_gstreamer10
        media_plugin_base
        ll::gstreamer10
        llplugin
        llmessage
        llcommon
        ll::pluginlibraries
)
