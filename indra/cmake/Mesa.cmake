include_guard()
add_library(ll::osmesa INTERFACE IMPORTED)

if(AL_BUILD_HEADLESS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(OSMESA REQUIRED IMPORTED_TARGET GLOBAL osmesa)
  target_link_libraries(ll::osmesa INTERFACE PkgConfig::OSMESA)
endif()
