# -*- cmake -*-

include(Variables)
include(GLEXT)
include(Prebuilt)

if (USESYSTEMLIBS OR LINUX)
  include(FindPkgConfig)
  pkg_check_modules(SDL REQUIRED sdl2)
else ()
  if (LINUX)
    use_prebuilt_binary(SDL)
    set (SDL_FOUND TRUE)
    set (SDL_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
    set (SDL_LIBRARIES SDL X11)
  endif (LINUX)
endif ()

if (SDL_FOUND)
  include_directories(${SDL_INCLUDE_DIRS})
endif (SDL_FOUND)

set(LLWINDOW_INCLUDE_DIRS
    ${GLEXT_INCLUDE_DIR}
    ${LIBS_OPEN_DIR}/llwindow
    )

if (BUILD_HEADLESS)
  set(LLWINDOW_HEADLESS_LIBRARIES
      llwindowheadless
      )
endif (BUILD_HEADLESS)

  set(LLWINDOW_LIBRARIES
      llwindow
      )
