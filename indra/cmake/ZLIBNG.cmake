# -*- cmake -*-

include(Prebuilt)

include_guard()
add_library( ll::zlib-ng INTERFACE IMPORTED )

if(USE_CONAN )
  target_link_libraries( ll::zlib-ng INTERFACE CONAN_PKG::zlib )
  return()
endif()

use_prebuilt_binary(zlib-ng)
if (WINDOWS)
  target_link_libraries( ll::zlib-ng INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/zlibd.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/zlib.lib)
else()
  target_link_libraries( ll::zlib-ng INTERFACE 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libz.a
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libz.a)
endif (WINDOWS)

target_include_directories( ll::zlib-ng SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/zlib)

add_library( ll::minizip-ng INTERFACE IMPORTED )

if(USE_CONAN )
  target_link_libraries( ll::minizip-ng INTERFACE CONAN_PKG::minizip-ng )
  return()
endif()

use_prebuilt_binary(minizip-ng)
if (WINDOWS)
  target_link_libraries( ll::minizip-ng INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libminizip.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libminizip.lib
      ll::zlib-ng)
else()
  target_link_libraries( ll::minizip-ng INTERFACE 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libminizip.a 
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libminizip.a 
      ll::zlib-ng)
endif (WINDOWS)

target_include_directories( ll::minizip-ng SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/minizip)

