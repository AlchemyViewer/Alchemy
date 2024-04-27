# -*- cmake -*-
include(Prebuilt)

include_guard()
add_library( ll::openjpeg INTERFACE IMPORTED )

use_system_binary(openjpeg)
use_prebuilt_binary(openjpeg)

if(WINDOWS)
  target_link_libraries(ll::openjpeg INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/openjp2.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/openjp2.lib)
else ()
  target_link_libraries(ll::openjpeg INTERFACE openjp2 )
endif ()
target_include_directories( ll::openjpeg SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/openjpeg)
