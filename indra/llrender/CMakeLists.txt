# -*- cmake -*-

set(
  llrender_SOURCES
  aldepthbounds.cpp
  alframefences.cpp
  algpubuffer.cpp
  algputimer.cpp
  alimageslot.cpp
  alprimitiveplot.cpp
  alquerypool.cpp
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
  aldepthbounds.h
  alframefences.h
  algpubuffer.h
  algputimer.h
  alimageslot.h
  alprimitiveplot.h
  alquerypool.h
  alrendertargetdesc.h
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

  # The state guard's checks on what llrender keeps for the process, for
  # each of its test binaries.
  add_library(llrender_test_checks OBJECT tests/llrender_test_checks.cpp)
  target_link_libraries(llrender_test_checks PRIVATE al::flags al_test_support llrender)
  set_target_properties(llrender_test_checks PROPERTIES FOLDER "Tests/llrender")

  # The suites that need no GL: arithmetic (where a local coordinate lands
  # under the UI transform; the matrix stack, the projection helpers and the
  # clip-plane classes; the GL floor's version test), the font tables and
  # shaping, and render target descriptions. alfontshaping holds both Unicode tables against each other, so
  # this binary links ICU, which llrender itself has no need of.
  al_add_test_binary(
    llrender_tests
    PROJECT llrender
    SOURCES
      tests/alfontcolrv1_test.cpp
      tests/alfontshaping_test.cpp
      tests/alrendertargetdesc_test.cpp
      tests/llfontbitmapcache_test.cpp
      tests/llfontfreetype_test.cpp
      tests/llfontregistry_test.cpp
      tests/llglfloor_test.cpp
      tests/llrendermatrix_test.cpp
      tests/lluitransform_test.cpp
    LIBRARIES llrender ll::icu
    SUPPORT llrender_test_checks llcommon_test_checks
    DEFINES ${font_defines}
  )

  # The GL suites that stand a context up for each test case and end the GL
  # classes with it, on LLWindowManager's hidden window: the paths of the
  # font libraries above that rasterize, and llimagegl, which also makes a
  # context again in the middle of a case. Never beside the shared context
  # below, whose classes they would end under it.
  al_add_test_binary(
    llrender_gl_fresh_tests
    PROJECT llrender
    SOURCES
      tests/alfontcolrv1_gl_test.cpp
      tests/alfontshaping_gl_test.cpp
      tests/llfontbitmapcache_gl_test.cpp
      tests/llfontfreetype_gl_test.cpp
      tests/llfontregistry_gl_test.cpp
      tests/llimagegl_test.cpp
    LIBRARIES llrender ll::icu
    SUPPORT llrender_test_checks llcommon_test_checks
    DEFINES ${font_defines}
    GL
  )

  # The GL suites that share one context for the process, each case putting
  # back what it changed of it:
  # - fonts drawn, cached and laid out (llfontgl, llfonttextcache), UI images
  #   and the immediate-mode 2D helpers' quads;
  # - the hidden window itself, and a worker thread's shared context on it;
  # - tessellation stages and the quad conventions the terrain relies on;
  # - the infinite reverse-Z projection: stored depth, round trip and depth
  #   order out to 100 km;
  # - the depth bounds test against the depth test it stands in for, and the
  #   depths a light's sphere spans;
  # - which fragment output reaches a render target's colour attachment,
  #   integer attachments cleared and drawn, targets kept, built again and
  #   re-pointed at a shared depth by their descriptions, and their labels;
  # - LLRender's cached clear colour and scissor box, and LLGLSScissor;
  # - LLGLSLShader's cache key, its uniform value cache, the compute path
  #   (programs built through the shader manager, their images, storage
  #   blocks and dispatches), integer textures through reserved samplers,
  #   what the debug validator says of images and stencil reads, and which
  #   shader objects a program owns and which the manager does;
  # - LLRender::drawArraysInstanced: attribute-less instanced geometry, placed
  #   by the matrices the draw syncs;
  # - ALGPUBuffer's store, LLRender's indirect buffer bindings over its
  #   lifetime, and the readback and stream rings;
  # - ALUniformBuffer's persistently mapped rings: the store each makes, how
  #   it is mapped, what the GPU reads where a slice is bound, and the scratch
  #   ring's segments reused once the frame each was left in has finished;
  # - ALGPUTimer: one span timed at a time, as timestamps and as Elapsed
  #   queries;
  # - ALFrameFences: one fence a frame, polled at the frame's end under
  #   Mesa's glthread and where its user asks elsewhere, and waits that stall
  #   only on frames the GPU has not finished;
  # - ALQueryPool: each kind's names kept to its target, a held target
  #   refused, results read once their frame has ended, spans, release and
  #   conditional draws;
  # - ALImageSlot: what a unit issues, re-issues and forgets as LLRender keeps
  #   it, and integer images;
  # - LLVertexBuffer: what a buffer draws straight after an upload, after a
  #   second upload and after a new size, and that an upload sends only the
  #   stores its map wrote; and LLRenderSphere's buffer drawn under the debug
  #   GL checks.
  al_add_test_binary(
    llrender_gl_shared_tests
    PROJECT llrender
    SOURCES
      tests/aldepthbounds_gl_test.cpp
      tests/alframefences_gl_test.cpp
      tests/algpubuffer_gl_test.cpp
      tests/algputimer_gl_test.cpp
      tests/alimageslot_gl_test.cpp
      tests/alinfiniteprojection_gl_test.cpp
      tests/alinstanceddraw_gl_test.cpp
      tests/alquerypool_gl_test.cpp
      tests/alrenderstate_gl_test.cpp
      tests/altessellation_gl_test.cpp
      tests/aluniformbuffer_gl_test.cpp
      tests/alwindowsdlheadless_test.cpp
      tests/llfontgl_test.cpp
      tests/llfonttextcache_test.cpp
      tests/llglslshader_gl_test.cpp
      tests/llrender2dutils_gl_test.cpp
      tests/llrendertarget_gl_test.cpp
      tests/llrendersphere_gl_test.cpp
      tests/lluiimage_test.cpp
      tests/llvertexbuffer_gl_test.cpp
    LIBRARIES llrender
    SUPPORT llrender_test_checks llcommon_test_checks
    DEFINES ${font_defines}
    GL
  )
endif()
