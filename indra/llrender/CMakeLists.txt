# -*- cmake -*-

set(llrender_SOURCE_FILES
    llcubemap.cpp
    llcubemaparray.cpp
    llfontbitmapcache.cpp
    llfontcolrv1.cpp
    llfontface.cpp
    llfontfreetype.cpp
    llfontgl.cpp
    llfontgpubatch.cpp
    llfontgpuglyphcache.cpp
    llfontgpushader.cpp
    llfontshaping.cpp
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
    llcubemap.h
    llcubemaparray.h
    llfontgl.h
    llfontvertexbuffer.h
    llfontface.h
    llfontfreetype.h
    llfontgpubatch.h
    llfontgpuglyphcache.h
    llfontgpushader.h
    llhbgpu.h
    llfontbitmapcache.h
    llfontcolrv1.h
    llfontregistry.h
    llfontshaping.h
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
  # at the bottom of that file activate. llfontcolrv1 stays on regular
  # llrender — it doesn't need GL.
  set(test_project llrender)
  set(test_libs llrender)
  if (BUILD_HEADLESS)
    set(registry_test_libs llrenderheadless)
  else()
    set(registry_test_libs llrender)
  endif()

  LL_ADD_INTEGRATION_TEST(llfontcolrv1 "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontregistry "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontbitmapcache "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontshaping "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontfreetype "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontgpu "" "${registry_test_libs}" "${test_project}")
  if (TARGET INTEGRATION_TEST_llfontgpu)
    # hb-gpu spike. libharfbuzz-gpu comes in transitively via ll::harfbuzz
    # (see indra/cmake/Harfbuzz.cmake); just hand the test the font dir.
    # Tests 1-3 (link/encode/shader-source) run with or without headless GL;
    # test 4 (GLSL compile) only activates under BUILD_HEADLESS below.
    target_compile_definitions(INTEGRATION_TEST_llfontgpu PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()
  LL_ADD_INTEGRATION_TEST(llfontgpuglyphcache "" "${registry_test_libs}" "${test_project}")
  if (TARGET INTEGRATION_TEST_llfontgpuglyphcache)
    # Encode/cache/evict tests (1-3) need no GL; the GL-upload test (4) only
    # activates under BUILD_HEADLESS below. hb-gpu arrives via ll::harfbuzz.
    target_compile_definitions(INTEGRATION_TEST_llfontgpuglyphcache PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()
  # Source-assembly test (1) needs no GL; the compile+link test (2) activates
  # under BUILD_HEADLESS below. hb-gpu arrives via ll::harfbuzz.
  LL_ADD_INTEGRATION_TEST(llfontgpushader "" "${registry_test_libs}" "${test_project}")
  LL_ADD_INTEGRATION_TEST(llfontgpubatch "" "${registry_test_libs}" "${test_project}")
  if (TARGET INTEGRATION_TEST_llfontgpubatch)
    # Geometry test (1) needs no GL; the render+readback test (2) activates
    # under BUILD_HEADLESS below. Both need a real font file.
    target_compile_definitions(INTEGRATION_TEST_llfontgpubatch PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()
  if (TARGET INTEGRATION_TEST_llfontcolrv1)
    # Hand the source-tree font directory to the test as a string literal.
    # Tests that need a real font file (Noto-COLRv1.ttf, InterVariable.woff2)
    # locate it relative to this dir; absent files cause the affected tests
    # to skip rather than fail, so a partial font set still runs the rest
    # of the suite.
    target_compile_definitions(INTEGRATION_TEST_llfontcolrv1 PRIVATE
        "LLFONT_TEST_DATA_DIR=\"${INDRA_SOURCE_DIR}/newview/fonts/\"")
  endif()
  if (TARGET INTEGRATION_TEST_llfontshaping)
    target_compile_definitions(INTEGRATION_TEST_llfontshaping PRIVATE
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

    if (TARGET INTEGRATION_TEST_llfontshaping)
      # llfontshaping's GL-gated monospace tests — same library swap
      # pattern as llfontbitmapcache and llfontregistry.
      target_compile_definitions(INTEGRATION_TEST_llfontshaping PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontfreetype)
      # llfontfreetype's GL-gated rasterizer tests need the OSMesa fixture.
      target_compile_definitions(INTEGRATION_TEST_llfontfreetype PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontgpu)
      # llfontgpu's GLSL-compile test (test<4>) needs an OSMesa GL context;
      # the binary already linked llrenderheadless via registry_test_libs.
      target_compile_definitions(INTEGRATION_TEST_llfontgpu PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontgpuglyphcache)
      # llfontgpuglyphcache's GL-upload test (test<4>) needs an OSMesa context.
      target_compile_definitions(INTEGRATION_TEST_llfontgpuglyphcache PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontgpushader)
      # llfontgpushader's compile+link test (test<2>) needs an OSMesa context.
      target_compile_definitions(INTEGRATION_TEST_llfontgpushader PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontgpubatch)
      # llfontgpubatch's render+readback test (test<2>) needs an OSMesa context.
      target_compile_definitions(INTEGRATION_TEST_llfontgpubatch PRIVATE
          ${headless_test_compile_defs})
    endif()

    if (TARGET INTEGRATION_TEST_llfontcolrv1)
      # llfontcolrv1's GL-gated tests cover the integration of the
      # painter with the font atlas — same library swap pattern as
      # llfontbitmapcache and llfontfreetype.
      target_compile_definitions(INTEGRATION_TEST_llfontcolrv1 PRIVATE
          ${headless_test_compile_defs})
    endif()

    LL_ADD_INTEGRATION_TEST(llfontgl "" "llrenderheadless" "${test_project}")
    if (TARGET INTEGRATION_TEST_llfontgl)
      target_compile_definitions(INTEGRATION_TEST_llfontgl PRIVATE
          ${headless_test_compile_defs})
    endif()

    # llfontvertexbuffer: pure-headless. The vertex buffer renders
    # text geometry through gGL; all tests need an OSMesa context.
    LL_ADD_INTEGRATION_TEST(llfontvertexbuffer "" "llrenderheadless" "${test_project}")
    if (TARGET INTEGRATION_TEST_llfontvertexbuffer)
      target_compile_definitions(INTEGRATION_TEST_llfontvertexbuffer PRIVATE
          ${headless_test_compile_defs})
    endif()

    LL_ADD_INTEGRATION_TEST(llimagegl "" "llrenderheadless" "${test_project}")
    if (TARGET INTEGRATION_TEST_llimagegl)
      target_compile_definitions(INTEGRATION_TEST_llimagegl PRIVATE
          ${headless_test_compile_defs})
    endif()
  endif()
endif()
