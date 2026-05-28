include_guard()

add_library( ll::glib_headers INTERFACE IMPORTED )

if( LINUX )
  find_package(PkgConfig REQUIRED)
  pkg_search_module(GLIB REQUIRED glib-2.0)

  target_include_directories( ll::glib_headers SYSTEM INTERFACE ${GLIB_INCLUDE_DIRS}  )
  target_compile_definitions( ll::glib_headers INTERFACE -DLL_GLIB=1)
endif()
