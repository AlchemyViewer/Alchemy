# -*- cmake -*-
include(Linking)
include(Prebuilt)

include_guard()
add_library( ll::libpng INTERFACE IMPORTED )

use_system_binary(libpng)
use_prebuilt_binary(libpng)
if (WINDOWS)
  target_link_libraries(ll::libpng INTERFACE
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libpng16d.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libpng16.lib
        )
elseif(LINUX)
  target_link_libraries(ll::libpng INTERFACE
        ${ARCH_PREBUILT_DIRS}/libpng16.a
        )
else()
  target_link_libraries(ll::libpng INTERFACE png16 )
endif()
target_include_directories( ll::libpng SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/libpng16)
