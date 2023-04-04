# -*- cmake -*-
include(Prebuilt)
# USE_KDU can be set when launching cmake as an option using the argument -DUSE_KDU:BOOL=ON
# When building using proprietary binaries though (i.e. having access to LL private servers), 
# we always build with KDU
#if (INSTALL_PROPRIETARY)
#  option(USE_KDU "Use Kakadu library." ON)
#endif (INSTALL_PROPRIETARY)

include_guard()
add_library( ll::kdu INTERFACE IMPORTED )

if (USE_KDU)
  use_prebuilt_binary(kdu)

  # Our KDU package is built with KDU_X86_INTRINSICS in its .vcxproj file.
  # Unless that macro is also set for every consumer build, KDU freaks out,
  # spamming the viewer log with alignment FUD.
  target_compile_definitions( ll::kdu INTERFACE KDU_X86_INTRINSICS=1 KDU_NO_THREADS KDU_NO_AVX KDU_NO_AVX2)

  if (WINDOWS)
    target_link_libraries( ll::kdu INTERFACE
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/kdud.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/kdu.lib
      )
  else (WINDOWS)
    target_link_libraries( ll::kdu INTERFACE libkdu.a)
  endif (WINDOWS)

  target_include_directories( ll::kdu SYSTEM INTERFACE
          ${LIBS_PREBUILT_DIR}/include/kdu
          )
endif (USE_KDU)
