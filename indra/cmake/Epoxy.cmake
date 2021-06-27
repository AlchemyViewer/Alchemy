# -*- cmake -*-
include(Linking)
include(Prebuilt)

if (USESYSTEMLIBS)
  include(FindPkgConfig)

  pkg_check_modules(EPOXY REQUIRED epoxy)
else (USESYSTEMLIBS)
  use_prebuilt_binary(libepoxy)
  set(EPOXY_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/)
  if (WINDOWS)
    set(EPOXY_LIBRARIES 
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/epoxy.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/epoxy.lib)
  else ()
    set(EPOXY_LIBRARIES epoxy)
  endif()
endif (USESYSTEMLIBS)