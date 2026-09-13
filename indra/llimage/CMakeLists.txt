# -*- cmake -*-

add_library(llimage STATIC)

target_sources(
  llimage
  PRIVATE
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
if(AL_USE_KDU)
  target_link_libraries(llimage PUBLIC llkdu)
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
  al_add_test(llimageworker UNIT PROJECT llimage)
endif()
