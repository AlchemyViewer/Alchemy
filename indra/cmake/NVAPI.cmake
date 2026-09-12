# -*- cmake -*-
include_guard()

# NVIDIA's NVAPI, for the GPU profile the Windows viewer applies at startup.
# Windows only; the target is empty elsewhere.
add_library(ll::nvapi INTERFACE IMPORTED)

if (WINDOWS)
  find_package(unofficial-nvapi CONFIG REQUIRED)
  target_link_libraries(ll::nvapi INTERFACE unofficial::nvapi::nvapi)
endif ()
