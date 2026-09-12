# -*- cmake -*-
include(PluginAPI)

### media_plugin_base
add_library(media_plugin_base STATIC)

target_sources(media_plugin_base
    PRIVATE
    media_plugin_base.cpp
    PUBLIC
    FILE_SET HEADERS
    FILES
    media_plugin_base.h
    )

target_link_libraries(media_plugin_base PRIVATE al::flags PUBLIC llplugin ll::pluginlibraries)
