# -*- cmake -*-
include(Prebuilt)

include_guard()
add_library( ll::xmlrpc-epi INTERFACE IMPORTED )

use_system_binary( xmlrpc-epi )

use_prebuilt_binary(xmlrpc-epi)
if (WINDOWS)
  target_compile_definitions( ll::xmlrpc-epi INTERFACE XMLRPCEPI_STATIC=1)
  target_link_libraries(ll::xmlrpc-epi INTERFACE
    debug ${ARCH_PREBUILT_DIRS_DEBUG}/xmlrpc-epid.lib
    optimized ${ARCH_PREBUILT_DIRS_RELEASE}/xmlrpc-epi.lib)
else()
  target_link_libraries(ll::xmlrpc-epi INTERFACE
    debug ${ARCH_PREBUILT_DIRS_DEBUG}/libxmlrpc-epi.a
    optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libxmlrpc-epi.a)
endif (WINDOWS)
target_include_directories( ll::xmlrpc-epi SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)
