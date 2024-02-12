# -*- cmake -*-

include_guard()

add_library( ll::pluginlibraries INTERFACE IMPORTED )

if (WINDOWS)
  target_link_libraries( ll::pluginlibraries INTERFACE
      wsock32
      ws2_32
      psapi
      advapi32
      user32
      wer
      )
elseif (DARWIN)
  include(CMakeFindFrameworks)
  find_library(COCOA_LIBRARY Cocoa)
  target_link_libraries( ll::pluginlibraries INTERFACE
      ${COCOA_LIBRARY}
       )
endif (WINDOWS)


