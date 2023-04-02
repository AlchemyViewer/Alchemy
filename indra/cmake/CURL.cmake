# -*- cmake -*-
include(Prebuilt)
include(Linking)

include_guard()
add_library( ll::libcurl INTERFACE IMPORTED )

use_system_binary(libcurl)
use_prebuilt_binary(curl)
if (WINDOWS)
    target_link_libraries(ll::libcurl INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcurld.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcurl.lib
      Normaliz.lib
      Iphlpapi.lib
    )
elseif(DARWIN)
    target_link_libraries(ll::libcurl INTERFACE 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcurld.a
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcurl.a
      resolv
    )
else ()
    target_link_libraries(ll::libcurl INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcurld.a
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcurl.a
      )
endif ()
target_include_directories( ll::libcurl SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)
