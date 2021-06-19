# -*- cmake -*-

add_subdirectory(base)
add_subdirectory(example)

if (USE_CEF)
    add_subdirectory(cef)
endif()
if (USE_VLC)
    add_subdirectory(libvlc)
endif()
