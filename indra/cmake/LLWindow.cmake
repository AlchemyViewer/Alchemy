# -*- cmake -*-

include(Variables)
include(Epoxy)
include(GLEXT)
include(SDL2)

set(LLWINDOW_INCLUDE_DIRS
    ${GLEXT_INCLUDE_DIR}
    ${EPOXY_INCLUDE_DIRS}
    ${SDL_INCLUDE_DIRS}
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
