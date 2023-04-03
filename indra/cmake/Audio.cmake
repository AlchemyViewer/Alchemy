# -*- cmake -*-
include(Linking)
include(Prebuilt)

include_guard()
add_library( ll::vorbis INTERFACE IMPORTED )

use_system_binary(vorbis)
use_prebuilt_binary(ogg_vorbis)
target_include_directories( ll::vorbis SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include )

if (WINDOWS)
  target_link_libraries(ll::vorbis INTERFACE
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libogg.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libogg.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbis.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbis.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbisfile.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbisfile.lib
	)
else (WINDOWS)
  target_link_libraries(ll::vorbis INTERFACE
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbisenc.a
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbisenc.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbisfile.a
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbisfile.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libvorbis.a
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libvorbis.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libogg.a
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libogg.a
        )
endif (WINDOWS)
