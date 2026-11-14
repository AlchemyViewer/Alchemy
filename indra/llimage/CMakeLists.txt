# -*- cmake -*-

add_library(llimage STATIC)

target_sources(
  llimage
  PRIVATE
    alj2clayout.cpp
    llimagebmp.cpp
    llimage.cpp
    llimagedimensionsinfo.cpp
    llimagedxt.cpp
    llimagefilter.cpp
    llimagej2c.cpp
    llimagejpeg.cpp
    llimagepng.cpp
    llimagetga.cpp
    llimagewebp.cpp
    llimageavif.cpp
    llimageworker.cpp
    llpngwrapper.cpp
)

target_sources(
  llimage
  PUBLIC
    FILE_SET HEADERS
      FILES
        alj2clayout.h
        llimage.h
        llimagebmp.h
        llimagedimensionsinfo.h
        llimagedxt.h
        llimagefilter.h
        llimagej2c.h
        llimagejpeg.h
        llimagepng.h
        llimagetga.h
        llimagewebp.h
        llimageavif.h
        llimageworker.h
        llmapimagetype.h
        llpngwrapper.h
)

target_link_libraries(llimage PRIVATE al::flags)
if(AL_USE_PNJ2K)
  target_link_libraries(llimage PUBLIC llimagej2crs)
else()
  target_link_libraries(llimage PUBLIC llimagej2coj)
endif()

target_link_libraries(
  llimage
  PUBLIC llfilesystem llmath llcommon ll::libwebp ll::libavif ll::libpng ll::libjpeg
)

target_precompile_headers(llimage REUSE_FROM llprecompiled)

# Add tests
if(AL_BUILD_TESTS)
  # It stubs the image classes the library defines, so it compiles the one
  # file it tests instead of linking the library.
  al_add_test_binary(
    llimageworker_test
    PROJECT llimage
    SOURCES tests/llimageworker_test.cpp
    UNIT llimageworker.cpp
    LIBRARIES llmath llcommon
  )

  # The library itself: the locks images are read and written under,
  # LLImageJ2C over the build's codec, and the J2C codestream layout and the
  # byte targets it gives. The layout's cases on one layer and on several
  # tiles encode through the build's codec's own API, pnj2k's or OpenJPEG's.
  al_add_test_binary(
    llimage_tests
    PROJECT llimage
    SOURCES tests/llimage_test.cpp tests/llimagej2c_test.cpp tests/alj2clayout_test.cpp
    LIBRARIES llimage ll::pnj2k ll::openjpeg
    DEFINES LL_PNJ2K=$<BOOL:${AL_USE_PNJ2K}> LL_OPENJPEG=$<BOOL:${AL_USE_OPENJPEG}>
  )
endif()
