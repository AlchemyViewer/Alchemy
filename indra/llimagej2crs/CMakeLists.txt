# -*- cmake -*-

# JPEG 2000 decoding and encoding with pnj2k, the Rust codec, over its C API.
add_library(llimagej2crs STATIC)

target_sources(llimagej2crs PRIVATE alimagej2crs.cpp PUBLIC FILE_SET HEADERS FILES alimagej2crs.h)

target_link_libraries(llimagej2crs PRIVATE al::flags ll::pnj2k PUBLIC llimage llcommon)
# pnj2k's library is checked for what it must not hold before this links it,
# and so before anything does; see cmake/Pnj2k.cmake.
add_dependencies(llimagej2crs pnj2k-audit)

target_precompile_headers(llimagej2crs REUSE_FROM llprecompiled)

if(AL_BUILD_TESTS)
  # Decodes and encodes through LLImageJ2C, held against the images encoded
  # and against pnj2k's own C API. The viewer's skin textures stand for
  # other encoders' codestreams, and the hidden [corpus] case reads the
  # textures AL_J2C_CORPUS_DIR names.
  al_add_test_binary(
    llimagej2crs_tests
    PROJECT llimagej2crs
    SOURCES tests/alimagej2crs_test.cpp
    LIBRARIES llimagej2crs ll::pnj2k llimage
    DEFINES "AL_TEST_SKIN_TEXTURES_DIR=\"${INDRA_SOURCE_DIR}/newview/skins/default/textures\""
  )
endif()
