# -*- cmake -*-
include_guard()
add_library( ll::kdu INTERFACE IMPORTED )

if (AL_USE_KDU)
  find_library(KDU_LIBRARY
    NAMES
    kdu
    kdu.lib
    libkdu.a
    REQUIRED)

  target_link_libraries(ll::kdu INTERFACE ${KDU_LIBRARY})

  find_path(KDU_INCLUDE_DIRS NAMES kdu_arch.h PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/kdu" REQUIRED NO_DEFAULT_PATH)
  target_include_directories( ll::kdu SYSTEM INTERFACE
          ${KDU_INCLUDE_DIRS}
          )
  target_compile_definitions(ll::kdu INTERFACE KDU_NO_THREADS=1)
endif (AL_USE_KDU)
