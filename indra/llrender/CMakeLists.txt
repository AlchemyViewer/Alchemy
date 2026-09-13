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

if(AL_BUILD_HEADLESS)
  add_library(llrenderheadless STATIC)
  target_sources(
    llrenderheadless
    PRIVATE ${llrender_SOURCES}
    PUBLIC FILE_SET HEADERS FILES ${llrender_HEADERS}
  )

  target_link_libraries(
    llrenderheadless
    PRIVATE al::flags
    PUBLIC
      llcommon
      llimage
      llmath
      llxml
      llfilesystem
      llwindowheadless
      ll::freetype
      ll::harfbuzz
      ll::plutosvg
      ll::glext
      ll::opengl
  )

  target_precompile_headers(llrenderheadless REUSE_FROM llprecompiled)
endif()

add_library(llrender STATIC)
target_sources(
  llrender
  PRIVATE ${llrender_SOURCES}
  PUBLIC FILE_SET HEADERS FILES ${llrender_HEADERS}
)

# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
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
  #
  # When AL_BUILD_HEADLESS=ON, the font tests are re-pointed at
  # llrenderheadless (with LL_MESA_HEADLESS=1) so the GL-gated groups at the
  # bottom of those files activate over an OSMesa context. lluitransform and
  # the pure-table tests need no GL either way.
  if(AL_BUILD_HEADLESS)
    set(registry_test_libs llrenderheadless)
  else()
    set(registry_test_libs llrender)
  endif()

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
  set(font_data_define "LLFONT_TEST_DATA_DIR=\"${font_test_data_dir}\"")
  set(
    font_app_defines
    "LLFONT_TEST_APP_DIR=\"${font_test_app_dir}\""
    "${font_data_define}"
    "LLFONT_TEST_FONTS_XML=\"${font_test_fonts_xml}\""
  )

  # Headless GL tests link llrenderheadless (which transitively pulls
  # llwindowheadless + ll::osmesa) so they can stand up an OSMesa GL context.
  # LL_MESA_HEADLESS=1 matches the lib's compile-time configuration; the
  # LLFONT_TEST_* defines feed the package font paths to the tests.
  if(AL_BUILD_HEADLESS)
    set(headless_defines LL_MESA_HEADLESS=1 ${font_app_defines})
  else()
    set(headless_defines)
  endif()

  # Arithmetic only: where a local coordinate lands under the UI transform.
  al_add_test(lluitransform PROJECT llrender LIBRARIES ${registry_test_libs})
  al_add_test(
    alfontcolrv1
    PROJECT llrender
    LIBRARIES ${registry_test_libs}
    DEFINES "${font_data_define}" ${headless_defines}
  )
  al_add_test(
    llfontregistry
    PROJECT llrender
    LIBRARIES ${registry_test_libs}
    DEFINES ${headless_defines}
  )
  al_add_test(
    llfontbitmapcache
    PROJECT llrender
    LIBRARIES ${registry_test_libs}
    DEFINES ${headless_defines}
  )
  # Both Unicode tables in one place, so the test can hold them against each
  # other. llrender itself has no need of ICU.
  al_add_test(
    alfontshaping
    PROJECT llrender
    LIBRARIES ${registry_test_libs} ll::icu
    DEFINES "${font_data_define}" ${headless_defines}
  )
  al_add_test(
    llfontfreetype
    PROJECT llrender
    LIBRARIES ${registry_test_libs}
    DEFINES "${font_data_define}" ${headless_defines}
  )

  # Wholly GL-backed suites. The context comes from SDL on a window that is
  # never shown -- the same GL the viewer runs on -- so these want plain
  # llrender and none of the headless library set, and they run everywhere
  # rather than only where OSMesa happens to be installed.
  set(gl_test_libs llrender ll::SDL3core)
  al_add_test(
    llfontgl
    PROJECT llrender
    LIBRARIES ${gl_test_libs}
    DEFINES LL_TEST_GL=1 ${font_app_defines}
  )
  al_add_test(
    llfonttextcache
    PROJECT llrender
    LIBRARIES ${gl_test_libs}
    DEFINES LL_TEST_GL=1 ${font_app_defines}
  )
  al_add_test(lluiimage PROJECT llrender LIBRARIES ${gl_test_libs})

  if(AL_BUILD_HEADLESS)
    al_add_test(llimagegl PROJECT llrender LIBRARIES llrenderheadless DEFINES ${headless_defines})
  endif()
endif()
