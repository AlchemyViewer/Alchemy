# -*- cmake -*-
include(Prebuilt)

if (NOT USESYSTEMLIBS)
  if (LINUX)
    use_prebuilt_binary(libuuid)
  endif (LINUX)
  use_prebuilt_binary(libhunspell)
  use_prebuilt_binary(slvoice)
endif(NOT USESYSTEMLIBS)

