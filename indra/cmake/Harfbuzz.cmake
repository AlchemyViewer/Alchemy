  include_guard()

  find_package(harfbuzz CONFIG REQUIRED)

  add_library(ll::harfbuzz INTERFACE IMPORTED)

  target_link_libraries(ll::harfbuzz INTERFACE harfbuzz::harfbuzz)

  find_package(PkgConfig)
  pkg_check_modules(HB-RASTER REQUIRED IMPORTED_TARGET GLOBAL harfbuzz-raster)
  target_link_libraries(ll::harfbuzz INTERFACE PkgConfig::HB-RASTER)
