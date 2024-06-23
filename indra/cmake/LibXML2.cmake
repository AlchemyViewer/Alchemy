# -*- cmake -*-

include(Prebuilt)

include_guard()
add_library( ll::libxml2 INTERFACE IMPORTED )

if(USE_CONAN )
  target_link_libraries( ll::libxml2 INTERFACE CONAN_PKG::libxml2 )
  return()
endif()

use_prebuilt_binary(libxml2)
if (WINDOWS)
    target_compile_definitions( ll::libxml2 INTERFACE LIBXML_STATIC=1)
    target_link_libraries( ll::libxml2 INTERFACE
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libxml2.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libxml2.lib)
elseif(DARWIN)
    target_link_libraries( ll::libxml2 INTERFACE xml2 iconv)
else()
    target_link_libraries( ll::libxml2 INTERFACE
        ${ARCH_PREBUILT_DIRS}/libxml2.a
    )
endif()

target_include_directories( ll::libxml2 SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/libxml2)
