# -*- cmake -*-
include(Prebuilt)

set(USE_NVAPI ON CACHE BOOL "Use NVAPI library.")

if (USE_NVAPI)
  if (WINDOWS)
    add_library( ll::nvapi INTERFACE IMPORTED )
    target_link_libraries( ll::nvapi INTERFACE nvapi)
    use_prebuilt_binary(nvapi)
    target_compile_definitions( ll::nvapi INTERFACE LL_NVAPI=1)
  endif (WINDOWS)
endif (USE_NVAPI)

