# -*- cmake -*-

# Render infrastructure over llrender that holds no viewer state: what the
# pipeline's render resources and programs are built from and when they
# change, the far plane, the water's field, wave heights and stencil bits,
# the depth pyramid, the light map's blur, the depth under the HUD,
# which placed reflection probes swallow automatic ones, snapshot cutouts,
# whether a spot light's shadow map is drawn again, what the sky's layers
# are worked out from along the view ray and the environment cube baked
# from the dome; and the pool that lends the frame's
# short-lived render targets by description. Whatever reads settings or the
# scene hands these their inputs as values.

set(
  alrender_SOURCES
  alcutoutimage.cpp
  aldepthpyramid.cpp
  alfarplane.cpp
  allightmapblur.cpp
  alondemandtarget.cpp
  alprobeeclipse.cpp
  alrenderinputrequests.cpp
  alrenderresourceplan.cpp
  alshaderinputs.cpp
  alskycube.cpp
  alskylayers.cpp
  alspotshadowcache.cpp
  alstarfield.cpp
  altransienttargetpool.cpp
  altransienttargets.cpp
  alwaterfield.cpp
  alwaterheightbake.cpp
)

set(
  alrender_HEADERS
  alcutoutimage.h
  aldepthpyramid.h
  alfarplane.h
  alframeend.h
  alhexlattice.h
  alhuddepth.h
  allightmapblur.h
  alondemandtarget.h
  alprobeeclipse.h
  alrenderinputrequests.h
  alrenderresourceinputs.h
  alrenderresourceplan.h
  alshaderinputs.h
  alskycube.h
  alskylayers.h
  alspotshadowcache.h
  alstarfield.h
  alstarfieldgenerate.h
  altransienttargetpool.h
  altransienttargets.h
  alwaterfield.h
  alwaterheightbake.h
  alwaterstencil.h
)

add_library(alrender STATIC)
target_sources(
  alrender
  PRIVATE ${alrender_SOURCES}
  PUBLIC FILE_SET HEADERS FILES ${alrender_HEADERS}
)

target_link_libraries(
  alrender
  PRIVATE al::flags ll::pocketfft
  PUBLIC llrender llimage llmath llcommon
)

target_precompile_headers(alrender REUSE_FROM llprecompiled)

if(AL_BUILD_TESTS)
  # The viewer's own shader files, read from the source tree, and the sky's as they were drawn before its composite.
  set(alrender_shader_dir "AL_TEST_SHADER_DIR=\"${INDRA_SOURCE_DIR}/newview/app_settings/shaders\"")
  set(
    alrender_skyraster_dir
    "AL_TEST_SKYRASTER_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/skyraster\""
  )

  # The suites that need no GL: the render resources' plan and the changes
  # that ask for it again, the transient target pool's policy, what the programs are built from, the far plane
  # and the constants its shaders share, the water's field and the wave
  # heights a normal map's slopes integrate to, snapshot cutouts, the spot
  # shadows' cache, how a display pass ends its frame, the star field, the
  # sky's layers and the environment cube's directions.
  al_add_test_binary(
    alrender_tests
    PROJECT alrender
    SOURCES
      tests/alcutoutimage_test.cpp
      tests/alfarplane_test.cpp
      tests/alframeend_test.cpp
      tests/alprobeeclipse_test.cpp
      tests/alrenderinputrequests_test.cpp
      tests/alrenderresourceplan_test.cpp
      tests/alshaderinputs_test.cpp
      tests/alskycube_test.cpp
      tests/alskylayers_test.cpp
      tests/alspotshadowcache_test.cpp
      tests/alstarfield_test.cpp
      tests/altransienttargetpool_test.cpp
      tests/alwaterfield_test.cpp
      tests/alwaterheightbake_test.cpp
    BENCH_SOURCES tests/alstarfield_bench.cpp
    LIBRARIES alrender
    SUPPORT llrender_test_checks llcommon_test_checks
    DEFINES ${alrender_shader_dir}
  )

  # The window's depth under the HUD, the water surface's draws over the
  # cells the field lays out, with the cull and the depth pyramid it reads
  # and the waves displaced by heights the bake integrates from a normal map,
  # a spot shadow draw's geometry from a real vertex buffer,
  # the light map's blur by one dispatch against its two fragment passes,
  # WindLight's dome along the view ray against the dome's mesh, the sky's
  # other layers along it against the programs that drew them and the whole
  # sky's composite against the sky they drew, the environment cube against
  # the dome it is baked from and the cube the CPU built, the stars
  # against the picture they left before their rework, FXAA on an RGB10_A2
  # source, the reflection probes' taps in one pass over them against their
  # list, the transient target pool's GL objects, shared depth and poison,
  # and the previews' and bakes' targets taken from it only when one draws,
  # on llrender's shared hidden window.
  al_add_test_binary(
    alrender_gl_tests
    PROJECT alrender
    SOURCES
      tests/alhuddepth_gl_test.cpp
      tests/allightmapblur_gl_test.cpp
      tests/alondemandtarget_gl_test.cpp
      tests/alskycomposite_gl_test.cpp
      tests/alskycube_gl_test.cpp
      tests/alskydome_gl_test.cpp
      tests/alskylayers_gl_test.cpp
      tests/alspotshadowcache_gl_test.cpp
      tests/alstarfield_gl_test.cpp
      tests/altransienttargetpool_gl_test.cpp
      tests/alwatersurface_gl_test.cpp
      tests/fxaa_gl_test.cpp
      tests/reflectionprobe_gl_test.cpp
    BENCH_SOURCES
      tests/alcompute_gl_bench.cpp
      tests/alondemandtarget_bench.cpp
      tests/alstarfield_gl_bench.cpp
      tests/alwatertargets_bench.cpp
      tests/reflectionprobe_gl_bench.cpp
    LIBRARIES alrender
    SUPPORT llrender_test_checks llcommon_test_checks
    DEFINES ${alrender_shader_dir} ${alrender_skyraster_dir}
    GL
  )
endif()
