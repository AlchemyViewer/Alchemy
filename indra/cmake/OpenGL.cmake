# -*- cmake -*-

include(Variables)
include(Prebuilt)

if (BUILD_HEADLESS)
  SET(OPENGL_HEADLESS_LIBRARIES OSMesa16 dl GLU)
endif (BUILD_HEADLESS)

find_package(OpenGL REQUIRED)

