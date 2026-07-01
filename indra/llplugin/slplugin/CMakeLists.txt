# -*- cmake -*-

# The generic SLPlugin launcher and its dlopen-based plugin loading have been
# removed: each media plugin is now its own host executable that statically links
# the plugin (see media_plugins/*/CMakeLists.txt). This directory no longer builds
# a target of its own - it just provides the shared host driver sources
# (slplugin.cpp, slplugin_static.cpp, slplugin_daemon.cpp, slplugin-objc.mm) that
# those per-plugin executables compile in by path.
