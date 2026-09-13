# -*- cmake -*-

include(PluginAPI)
include(GLIB)

include(GStreamer10Plugin)

### media_plugin_gstreamer10 - dedicated host executable that statically links the
### GStreamer media plugin together with the slplugin host driver (no dlopen). This
### plugin is only built on UNIX-but-not-Apple (see AL_BUILD_GSTREAMER_PLUGIN).

add_executable(media_plugin_gstreamer10)

target_sources(
  media_plugin_gstreamer10
  PRIVATE
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin.cpp
    ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin_static.cpp
    media_plugin_gstreamer10.cpp
    llmediaimplgstreamer_syms.h
)

target_link_libraries(
  media_plugin_gstreamer10
  PRIVATE
    al::flags
    media_plugin_base
    ll::gstreamer10
    llplugin
    llmessage
    llcommon
    ll::pluginlibraries
)
