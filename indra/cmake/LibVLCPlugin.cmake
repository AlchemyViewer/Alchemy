# -*- cmake -*-
include(Linking)
include(Prebuilt)

include_guard()
add_library( ll::libvlc INTERFACE IMPORTED )


use_prebuilt_binary(vlc-bin)
target_include_directories( ll::libvlc INTERFACE ${LIBS_PREBUILT_DIR}/include/vlc)

if (WINDOWS)
    target_link_libraries( ll::libvlc INTERFACE
            ${ARCH_PREBUILT_DIRS_RELEASE}/libvlc.lib
            ${ARCH_PREBUILT_DIRS_RELEASE}/libvlccore.lib
    )
elseif (DARWIN)
    target_link_libraries( ll::libvlc INTERFACE
            libvlc.dylib
            libvlccore.dylib
    )
elseif (LINUX)
# Specify a full path to make sure we get a static link
    target_link_libraries( ll::libvlc INTERFACE
            ${LIBS_PREBUILT_DIR}/lib/libvlc.a
            ${LIBS_PREBUILT_DIR}/lib/libvlccore.a
    )
endif ()
