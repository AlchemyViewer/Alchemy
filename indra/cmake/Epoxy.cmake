# -*- cmake -*-
include(Prebuilt)

if (USESYSTEMLIBS)
  include(FindPkgConfig)

  pkg_check_modules(EPOXY REQUIRED epoxy)
else (USESYSTEMLIBS)
  use_prebuilt_binary(libepoxy)
  set(EPOXY_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/)
  set(EPOXY_LIBRARIES epoxy)
endif (USESYSTEMLIBS)