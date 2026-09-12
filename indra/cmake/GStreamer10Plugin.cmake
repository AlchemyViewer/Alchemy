# -*- cmake -*-

include_guard()

include(GLIB)

add_library( ll::gstreamer10 INTERFACE IMPORTED )

if (LINUX)
  find_package(PkgConfig REQUIRED)

  pkg_check_modules(GSTREAMER10 REQUIRED gstreamer-1.0)
  pkg_check_modules(GSTREAMER10_PLUGINS_BASE REQUIRED gstreamer-plugins-base-1.0)

  target_include_directories( ll::gstreamer10 SYSTEM INTERFACE ${GSTREAMER10_INCLUDE_DIRS})
  target_link_libraries( ll::gstreamer10 INTERFACE  ll::glib_headers)

endif ()
