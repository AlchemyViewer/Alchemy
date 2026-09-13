# -*- cmake -*-

# Integration tests of the llimage library (JPEG2000, PNG, jpeg, etc... images reading and writing)
if(AL_BUILD_TESTS)
  add_executable(llimage_libtest)

  target_sources(llimage_libtest PRIVATE llimage_libtest.cpp llimage_libtest.h)

  set_target_properties(llimage_libtest PROPERTIES FOLDER "Tests")

  # Libraries on which this application depends on
  # Sort by high-level to low-level
  target_link_libraries(
    llimage_libtest
    PRIVATE al::flags llcommon llfilesystem llmath llimage
  )

  if(AL_BUILD_VIEWER)
    # Ensure people working on the viewer don't break this library
    add_dependencies(${AL_VIEWER_BINARY_NAME} llimage_libtest)
  endif()
endif()
