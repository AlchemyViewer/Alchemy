include_guard()

add_library( ll::dbus INTERFACE IMPORTED )

if( LINUX )
  find_package(PkgConfig)
  pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET GLOBAL dbus-1)

  target_link_libraries( ll::dbus INTERFACE PkgConfig::DBUS )
  target_compile_definitions( ll::dbus INTERFACE -DLL_DBUS=1 )
endif()
