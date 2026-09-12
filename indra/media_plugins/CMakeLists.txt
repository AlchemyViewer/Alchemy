# -*- cmake -*-

add_subdirectory(base)

if (AL_BUILD_EXAMPLE_PLUGIN)
    add_subdirectory(example)
endif()

if (AL_BUILD_CEF_PLUGIN)
    add_subdirectory(cef)
endif()

if (AL_BUILD_VLC_PLUGIN)
    add_subdirectory(libvlc)
endif()

if (AL_BUILD_GSTREAMER_PLUGIN)
    add_subdirectory(gstreamer10)
endif ()
