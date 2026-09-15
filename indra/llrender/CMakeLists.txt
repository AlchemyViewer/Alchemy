# -*- cmake -*-

set(
  llrender_SOURCES
  alsamplerstate.cpp
  altexture3d.cpp
  altextureslot.cpp
  alfontcolrv1.cpp
  alfontface.cpp
  alfontshaping.cpp
  aluniformbuffer.cpp
  llcubemap.cpp
  llcubemaparray.cpp
  llfontbitmapcache.cpp
  llfontfreetype.cpp
  llfontgl.cpp
  llfonttextcache.cpp
  llfontregistry.cpp
  llgl.cpp
  llglslshader.cpp
  llgltexture.cpp
  llimagegl.cpp
  llrender.cpp
  llrender2dutils.cpp
  llrendernavprim.cpp
  llrendersphere.cpp
  llrendertarget.cpp
  llshadermgr.cpp
  lltexture.cpp
  lltexturemanagerbridge.cpp
  lluiimage.cpp
  llvertexbuffer.cpp
  llglcommonfunc.cpp
)

set(
  llrender_HEADERS
  alsamplerstate.h
  altexture3d.h
  altextureslot.h
  alfontcolrv1.h
  alfontface.h
  alfontshaping.h
  aluniformbuffer.h
  llcubemap.h
  llcubemaparray.h
  llfontgl.h
  llfonttextcache.h
  llfontfreetype.h
  llfontbitmapcache.h
  llfontregistry.h
  llgl.h
  llglheaders.h
  llglslshader.h
  llglstates.h
  llgltexture.h
  llgltypes.h
  llimagegl.h
  llrender.h
  llrender2dutils.h
  llrendernavprim.h
  llrendersphere.h
  llrendertarget.h
  llshadermgr.h
  lltexture.h
  lltexturemanagerbridge.h
  lluiimage.h
  lluiimage.inl
  llvertexbuffer.h
  llglcommonfunc.h
)

add_library(llrender STATIC)
target_sources(
  llrender
  PRIVATE ${llrender_SOURCES}
  PUBLIC FILE_SET HEADERS FILES ${llrender_HEADERS}
)

target_link_libraries(
  llrender
  PRIVATE al::flags
  PUBLIC
    llcommon
    llimage
    llmath
    llfilesystem
    llxml
    llwindow
    ll::freetype
    ll::harfbuzz
    ll::plutosvg
    ll::glext
)

target_precompile_headers(llrender REUSE_FROM llprecompiled)

# llgl.cpp talks to WGL directly on Windows (wglGetCurrentDC for the
# extensions string) even when SDL owns context creation, so keep the
# OpenGL link explicit there regardless of the window backend.
if(DARWIN OR WINDOWS OR NOT AL_USE_SDL_WINDOW)
  target_link_libraries(llrender PUBLIC ll::opengl)
endif()

if(AL_BUILD_TESTS)
  # llrender pulls in llcommon / llimage / llmath / llfilesystem / llxml /
  # llwindow + ll::freetype / ll::harfbuzz / ll::plutosvg / ll::glext via
  # its own target_link_libraries; transitive linking propagates them to
  # consumers, so listing llrender alone is enough.

  # The bundled faces ship in the alchemy-fonts package; newview stages a
  # copy into its source tree only when the viewer itself is configured,
  # so the tests read the package directly. ALCHEMY_FONTS_DIR ends in
  # "fonts", the leaf LLFontGL::getFontPathLocal appends to the app dir,
  # so its parent stands in for the application directory.
  find_package(unofficial-alchemy-fonts CONFIG REQUIRED)
  get_filename_component(font_test_app_dir "${ALCHEMY_FONTS_DIR}" DIRECTORY)
  set(font_test_data_dir "${ALCHEMY_FONTS_DIR}/")
  set(font_test_fonts_xml "${INDRA_SOURCE_DIR}/newview/skins/default/xui/en/fonts.xml")

  # The font directory reaches a test as a string literal. Tests that need a
  # real font file (Noto-COLRv1.ttf, InterVariable.woff2) locate it relative
  # to this dir; absent files cause the affected tests to skip rather than
  # fail, so a partial font set still runs the rest of the suite.
  set(
    font_defines
    "LLFONT_TEST_APP_DIR=\"${font_test_app_dir}\""
    "LLFONT_TEST_DATA_DIR=\"${font_test_data_dir}\""
    "LLFONT_TEST_FONTS_XML=\"${font_test_fonts_xml}\""
  )

  # Arithmetic only: where a local coordinate lands under the UI transform.
  al_add_test(lluitransform PROJECT llrender LIBRARIES llrender)

  # Arithmetic only: the matrix stack, the projection helpers and the
  # clip-plane classes against the scalar matrix and the formulas.
  al_add_test(llrendermatrix PROJECT llrender LIBRARIES llrender)

  # The font suites: tables and shaping, no GL.
  foreach(test alfontcolrv1 llfontregistry llfontbitmapcache llfontfreetype)
    al_add_test(${test} PROJECT llrender LIBRARIES llrender DEFINES ${font_defines})
  endforeach()
  # Both Unicode tables in one place, so the test can hold them against each
  # other. llrender itself has no need of ICU.
  al_add_test(alfontshaping PROJECT llrender LIBRARIES llrender ll::icu DEFINES ${font_defines})

  # The GL-backed suites, on LLWindowManager's hidden window: the paths of
  # the four font libraries above that rasterize, and the wholly-GL ones.
  foreach(
    test
    alfontcolrv1_gl
    llfontregistry_gl
    llfontbitmapcache_gl
    llfontfreetype_gl
    llfontgl
    llfonttextcache
    llimagegl
  )
    al_add_test(${test} GL PROJECT llrender LIBRARIES llrender DEFINES ${font_defines})
  endforeach()
  al_add_test(
    alfontshaping_gl
    GL
    PROJECT llrender
    LIBRARIES llrender ll::icu
    DEFINES ${font_defines}
  )
  al_add_test(lluiimage GL PROJECT llrender LIBRARIES llrender)
  # The hidden window itself, and a worker thread's shared context on it.
  al_add_test(alwindowsdlheadless GL PROJECT llrender LIBRARIES llrender)
endif()
