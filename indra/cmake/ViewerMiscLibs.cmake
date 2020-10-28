# -*- cmake -*-
include(Prebuilt)

if (NOT USESYSTEMLIBS)
  use_prebuilt_binary(libhunspell)
  use_prebuilt_binary(slvoice)
endif(NOT USESYSTEMLIBS)

