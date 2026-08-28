# -*- cmake -*-

set(llrender_SOURCE_FILES
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
    llfontvertexbuffer.cpp
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

set(llrender_HEADER_FILES
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
    llfontvertexbuffer.h
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

list(APPEND llrender_SOURCE_FILES ${llrender_HEADER_FILES})

if (BUILD_HEADLESS)
  add_library (llrenderheadless STATIC
    ${llrender_SOURCE_FILES}
    )
  target_include_directories(llrenderheadless INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

  target_link_libraries(llrenderheadless
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
          OpenGL::GL
          )

  target_precompile_headers(llrenderheadless REUSE_FROM llprecompiled)
endif (BUILD_HEADLESS)

add_library (llrender STATIC ${llrender_SOURCE_FILES})
target_include_directories(llrender INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
target_link_libraries(llrender
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
if (DARWIN OR WINDOWS OR NOT USE_SDL_WINDOW)
  target_link_libraries(llrender
          OpenGL::GL
          )
endif()

if (BUILD_TESTING)
  # llrender pulls in llcommon / llimage / llmath / llfilesystem / llxml /
  # llwindow + ll::freetype / ll::harfbuzz / ll::plutosvg / ll::glext via
  # its own target_link_libraries; transitive linking propagates them to
  # consumers, so listing llrender alone is enough.
  #
  # When BUILD_HEADLESS=ON, llfontregistry_test gets re-pointed at
  # llrenderheadless (with LL_MESA_HEADLESS=1) so the trailing GL tests
  # at the bottom of that file activate. alfontcolrv1 stays on regular
  # llrender — it doesn't need GL.
  set(test_project llrender)
  set(test_libs llrender)
  if (BUILD_HEADLESS)
    set(registry_test_libs llrenderheadless)
  else()
    set(registry_test_libs llrender)
  endif()

  LL_ADD_INTEGRATION_TEST(alfontcolrv1 "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontregistry "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontbitmapcache "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(alfontshaping "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontfreetype "" "${registry_test_libs}" "${test_project}")

  # Wholly GL-backed suites. The context comes from SDL on a window that is
  # never shown -- the same GL the viewer runs on -- so these want plain
  # llrender and none of the headless library set, and they run everywhere
  # rather than only where OSMesa happens to be installed.
  set(gl_test_libs llrender ll::SDL3core)
  set(gl_test_compile_defs
      LL_TEST_GL=1
      "LLFONT_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
      "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\""
      "LLFONT_TEST_FONTS_XML=\"${INDRA_SOURCE_DIR}/newview/skins/default/xui/en/fonts.xml\"")

  LL_ADD_INTEGRATION_TEST(llfontgl "" "${gl_test_libs}" "${test_project}")
  if (TARGET INTEGRATION_TEST_llfontgl)
    target_compile_definitions(INTEGRATION_TEST_llfontgl PRIVATE ${gl_test_compile_defs})
  endif()

  LL_ADD_INTEGRATION_TEST(llfontvertexbuffer "" "${gl_test_libs}" "${test_project}")
  if (TARGET INTEGRATION_TEST_llfontvertexbuffer)
    target_compile_definitions(INTEGRATION_TEST_llfontvertexbuffer PRIVATE ${gl_test_compile_defs})
  endif()
  if (TARGET INTEGRATION_TEST_alfontcolrv1)
    # Hand the source-tree font directory to the test as a string literal.
    # Tests that need a real font file (Noto-COLRv1.ttf, InterVariable.woff2)
    # locate it relative to this dir; absent files cause the affected tests
    # to skip rather than fail, so a partial font set still runs the rest
    # of the suite.
    target_compile_definitions(INTEGRATION_TEST_alfontcolrv1 PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()
  if (TARGET INTEGRATION_TEST_alfontshaping)
    target_compile_definitions(INTEGRATION_TEST_alfontshaping PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()
  if (TARGET INTEGRATION_TEST_llfontfreetype)
    target_compile_definitions(INTEGRATION_TEST_llfontfreetype PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()

  if (BUILD_HEADLESS)
    # Headless GL tests: link against llrenderheadless (which transitively
    # pulls llwindowheadless + ll::osmesa) so the tests can stand up an
    # OSMesa GL context and exercise GL-backed code paths. LL_MESA_HEADLESS=1
    # matches the lib's compile-time configuration; the LLFONT_TEST_*
    # defs feed source-tree font paths to the tests.
    set(headless_test_compile_defs
        LL_MESA_HEADLESS=1
        "LLFONT_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\""
        "LLFONT_TEST_FONTS_XML=\"${INDRA_SOURCE_DIR}/newview/skins/default/xui/en/fonts.xml\"")

    if (TARGET INTEGRATION_TEST_llfontregistry)
      # llfontregistry's GL tests live in the same source file under
      # #if LL_MESA_HEADLESS — same defs as the standalone llfontgl test.
      target_compile_definitions(INTEGRATION_TEST_llfontregistry PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontbitmapcache)
      # llfontbitmapcache's GL-gated group activates with these defs;
      # the binary already linked llrenderheadless above via registry_test_libs.
      target_compile_definitions(INTEGRATION_TEST_llfontbitmapcache PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_alfontshaping)
      # alfontshaping's GL-gated monospace tests — same library swap
      # pattern as llfontbitmapcache and llfontregistry.
      target_compile_definitions(INTEGRATION_TEST_alfontshaping PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontfreetype)
      # llfontfreetype's GL-gated rasterizer tests need the OSMesa fixture.
      target_compile_definitions(INTEGRATION_TEST_llfontfreetype PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_alfontcolrv1)
      # alfontcolrv1's GL-gated tests cover the integration of the
      # painter with the font atlas — same library swap pattern as
      # llfontbitmapcache and llfontfreetype.
      target_compile_definitions(INTEGRATION_TEST_alfontcolrv1 PRIVATE
          ${headless_test_compile_defs})
    endif()

    LL_ADD_INTEGRATION_TEST(llimagegl "" "llrenderheadless" "${test_project}")
    if (TARGET INTEGRATION_TEST_llimagegl)
      target_compile_definitions(INTEGRATION_TEST_llimagegl PRIVATE
          ${headless_test_compile_defs})
    endif()
  endif()
endif()
