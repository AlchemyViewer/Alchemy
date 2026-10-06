# -*- cmake -*-
#
# FMOD is licensed per product from fmod.com, so its port, fmodstudio, lives
# in AlchemyViewer's private vcpkg registry: AL_USE_FMODSTUDIO asks for it
# through the manifest's fmodstudio feature, and the build needs read access
# to that registry. The port's target takes FMOD's logging build, fmodL, in
# the Debug configuration and fmod in every other.
include_guard()
add_library(ll::fmodstudio INTERFACE IMPORTED)

if(AL_USE_FMODSTUDIO)
  find_package(unofficial-fmodstudio CONFIG REQUIRED)

  target_compile_definitions(ll::fmodstudio INTERFACE LL_FMODSTUDIO=1)
  # A shared library on every platform; ViewerInstall.cmake ships it.
  target_link_libraries(ll::fmodstudio INTERFACE unofficial::fmodstudio::fmod)
endif()
