# -*- cmake -*-
include(Linking)
if (USESYSTEMLIBS OR LINUX)
    include(FindPkgConfig)

    pkg_check_modules(VLC REQUIRED libvlc)
else ()
    include(Prebuilt)

    use_prebuilt_binary(vlc-bin)
    set(VLC_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/vlc)

    if (WINDOWS)
        set(VLC_LIBRARIES
                libvlc.lib
                libvlccore.lib
                )
    elseif (DARWIN)
        set(VLC_LIBRARIES
                libvlc.dylib
                libvlccore.dylib
                )
    elseif (LINUX)
        # Specify a full path to make sure we get a static link
        set(VLC_LIBRARIES
                ${LIBS_PREBUILT_DIR}/lib/libvlc.a
                ${LIBS_PREBUILT_DIR}/lib/libvlccore.a
                )
    endif (WINDOWS)
endif ()
