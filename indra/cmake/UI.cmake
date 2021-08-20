# -*- cmake -*-
include(Prebuilt)
include(FreeType)

if (LINUX)
  include(FindPkgConfig)

  option(USE_X11 "Enable extra X11 support code" OFF)

  if (USESYSTEMLIBS)
    set(PKGCONFIG_PACKAGES
        atk
        cairo
        gdk-2.0
        gdk-pixbuf-2.0
        glib-2.0
        gmodule-2.0
        gtk+-2.0
        gthread-2.0
        pango
        pangoft2
        pangox
        )

    if(USE_X11)
      list(APPEND PKGCONFIG_PACKAGES 
            pangoxft
            x11
            xinerama
            )
    endif()
      
    foreach(pkg ${PKGCONFIG_PACKAGES})
      pkg_check_modules(${pkg} REQUIRED ${pkg})
      include_directories(${${pkg}_INCLUDE_DIRS})
      link_directories(${${pkg}_LIBRARY_DIRS})
      list(APPEND UI_LIBRARIES ${${pkg}_LIBRARIES})
      add_definitions(${${pkg}_CFLAGS_OTHERS})
    endforeach(pkg)
  else (USESYSTEMLIBS)
      set(PKGCONFIG_PACKAGES
          gdk-3.0
          gtk+-3.0
          x11
          xinerama
          )

      if(USE_X11)
        list(APPEND PKGCONFIG_PACKAGES 
              x11
              xinerama
              )
      endif()

      foreach(pkg ${PKGCONFIG_PACKAGES})
        pkg_check_modules(${pkg} REQUIRED ${pkg})
        include_directories(${${pkg}_INCLUDE_DIRS})
        link_directories(${${pkg}_LIBRARY_DIRS})
        list(APPEND UI_LIBRARIES ${${pkg}_LIBRARIES})
        add_definitions(${${pkg}_CFLAGS_OTHERS})
      endforeach(pkg)
  endif (USESYSTEMLIBS)

  if(USE_X11)
    add_definitions(-DLL_X11=1)
  endif()

  # Always enable gtk in linux
  add_definitions(-DLL_GTK=1)
endif (LINUX)