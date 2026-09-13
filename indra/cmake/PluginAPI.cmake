# -*- cmake -*-
include_guard()


add_library( ll::pluginlibraries INTERFACE IMPORTED )

if (WINDOWS)
  target_link_libraries( ll::pluginlibraries INTERFACE
      wsock32
      ws2_32
      Iphlpapi
      psapi
      advapi32
      user32
      )
endif (WINDOWS)

target_link_libraries( ll::pluginlibraries INTERFACE ll::opengl)

target_include_directories( ll::pluginlibraries INTERFACE ${INDRA_SOURCE_DIR}/llimage ${INDRA_SOURCE_DIR}/llrender)
