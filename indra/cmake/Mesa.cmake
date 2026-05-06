include_guard()
if(BUILD_HEADLESS)
  add_library(ll::osmesa INTERFACE IMPORTED)

  find_package(PkgConfig)
  pkg_check_modules(OSMESA REQUIRED IMPORTED_TARGET GLOBAL osmesa)
  target_link_libraries(ll::osmesa INTERFACE PkgConfig::OSMESA)
endif()
