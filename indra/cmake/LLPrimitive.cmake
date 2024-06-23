# -*- cmake -*-

# these should be moved to their own cmake file
include(Prebuilt)
include(Boost)
include(LibXML2)
include(ZLIBNG)

include_guard()

add_library( ll::colladadom INTERFACE IMPORTED )

# ND, needs fixup in collada conan pkg
if( USE_CONAN )
  target_include_directories( ll::colladadom SYSTEM INTERFACE
    "${CONAN_INCLUDE_DIRS_COLLADADOM}/collada-dom/"
    "${CONAN_INCLUDE_DIRS_COLLADADOM}/collada-dom/1.4/" )
endif()

use_system_binary( colladadom )
use_prebuilt_binary(colladadom)

target_include_directories( ll::colladadom SYSTEM INTERFACE
        ${LIBS_PREBUILT_DIR}/include/collada
        ${LIBS_PREBUILT_DIR}/include/collada/1.4
        )
if (WINDOWS)
    target_link_libraries(ll::colladadom INTERFACE
              debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcollada14dom23-sd.lib
              optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcollada14dom23-s.lib
              ll::boost ll::libxml2 ll::minizip-ng )
elseif (DARWIN)
    target_link_libraries(ll::colladadom INTERFACE collada14dom ll::boost ll::libxml2 ll::minizip-ng)
elseif (LINUX)
    target_link_libraries(ll::colladadom INTERFACE ${ARCH_PREBUILT_DIRS}/libcollada14dom.a ll::boost ll::libxml2 ll::minizip-ng)
endif()
