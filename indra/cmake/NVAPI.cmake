# -*- cmake -*-
include_guard()

add_library(ll::nvapi INTERFACE IMPORTED)

if (USE_NVAPI)
  if (WINDOWS)
    find_package(unofficial-nvapi CONFIG REQUIRED)
    target_link_libraries(ll::nvapi INTERFACE unofficial::nvapi::nvapi)
  endif (WINDOWS)
endif (USE_NVAPI)

