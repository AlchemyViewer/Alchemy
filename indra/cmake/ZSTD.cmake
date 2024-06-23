# -*- cmake -*-

include(Prebuilt)

include_guard()
add_library( ll::zstd INTERFACE IMPORTED )

if(USE_CONAN )
  target_link_libraries( ll::zstd INTERFACE CONAN_PKG::zlib )
  return()
endif()

use_prebuilt_binary(zstd)
if (WINDOWS)
  target_link_libraries( ll::zstd INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/zstd_static.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/zstd_static.lib)
elseif (LINUX)
  target_link_libraries( ll::zstd INTERFACE
      ${ARCH_PREBUILT_DIRS}/libzstd.a)
else()
  target_link_libraries( ll::zstd INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libzstd.a
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libzstd.a)
endif (WINDOWS)

target_include_directories( ll::zstd SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include)
