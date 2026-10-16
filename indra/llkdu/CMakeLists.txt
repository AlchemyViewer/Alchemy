# -*- cmake -*-

if(AL_USE_KDU)
  add_library(llkdu STATIC)

  target_sources(
    llkdu
    PRIVATE llimagej2ckdu.cpp llkdumem.cpp
    PUBLIC FILE_SET HEADERS FILES llimagej2ckdu.h llkdumem.h
  )

  target_link_libraries(llkdu PRIVATE al::flags PUBLIC ll::kdu llimage llcommon)

  # Add tests
  if(AL_BUILD_TESTS)
    get_property(llimage_include_dir TARGET llimage PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    # The codec, compiled into its test, over the test's stubs of llimage
    # and of the KDU calls it makes. Built only with KDU, which has no port
    # yet: this has not been compiled since it moved to Catch2.
    al_add_test_binary(
      llimagej2ckdu_test
      PROJECT llkdu
      SOURCES tests/llimagej2ckdu_test.cpp
      UNIT llimagej2ckdu.cpp
      LIBRARIES ll::kdu llmath llcommon
      INCLUDES ${llimage_include_dir}
    )
  endif()
endif()
