# -*- cmake -*-

# Integration tests of the llimage library (JPEG2000, PNG, jpeg, etc... images reading and writing)
if(AL_BUILD_TESTS)
  # Run from the build tree, as a test binary is, so without vcpkg's
  # app-local step.
  set(VCPKG_APPLOCAL_DEPS OFF)
  add_executable(llimage_libtest)

  target_sources(llimage_libtest PRIVATE llimage_libtest.cpp llimage_libtest.h)

  set_target_properties(llimage_libtest PROPERTIES FOLDER "Tests")

  target_link_libraries(
    llimage_libtest
    PRIVATE al::flags llcommon llfilesystem llmath llimage
  )

  if(AL_BUILD_VIEWER)
    # Ensure people working on the viewer don't break this library
    add_dependencies(${AL_VIEWER_BINARY_NAME} llimage_libtest)
  endif()
endif()
