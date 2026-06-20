  include_guard()

  find_package(harfbuzz CONFIG REQUIRED)

  add_library(ll::harfbuzz INTERFACE IMPORTED)

  target_link_libraries(ll::harfbuzz INTERFACE harfbuzz::harfbuzz)

  find_package(PkgConfig)
  pkg_check_modules(HB-RASTER REQUIRED IMPORTED_TARGET GLOBAL harfbuzz-raster)
  target_link_libraries(ll::harfbuzz INTERFACE PkgConfig::HB-RASTER)

  # libharfbuzz-gpu (hb-gpu): analytic, atlas-free glyph rendering. Built by
  # default in the HarfBuzz 14.x port (gpu/raster/vector are enabled, not
  # gated behind experimental_api). harfbuzz-gpu.dll ships automatically via
  # VCPKG_APPLOCAL_DEPS + viewer_manifest's *.dll glob, same as harfbuzz-raster.
  #
  # The compile-time capability seam (LL_HAS_HB_GPU) is NOT a propagated -D: it
  # is derived in indra/llrender/llhbgpu.h from HB_VERSION_ATLEAST, so it can't
  # desync the per-target define set against the shared precompiled header.
  pkg_check_modules(HB-GPU REQUIRED IMPORTED_TARGET GLOBAL harfbuzz-gpu)
  target_link_libraries(ll::harfbuzz INTERFACE PkgConfig::HB-GPU)
