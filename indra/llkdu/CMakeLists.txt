# -*- cmake -*-

if (AL_USE_KDU)
  add_library (llkdu STATIC)


target_sources(llkdu
    PRIVATE
    llimagej2ckdu.cpp
    llkdumem.cpp

    PUBLIC
    FILE_SET HEADERS
    FILES
    llimagej2ckdu.h
    llkdumem.h
    )

  target_link_libraries(llkdu PRIVATE al::flags PUBLIC ll::kdu llimage llcommon)

  # Add tests
  if (AL_BUILD_TESTS)
    get_property(llimage_include_dir TARGET llimage PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    al_add_test(llimagej2ckdu UNIT PROJECT llkdu
      LIBRARIES ll::kdu llcommon
      INCLUDES ${llimage_include_dir})
  endif ()
endif ()
