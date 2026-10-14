# -*- cmake -*-

add_library(llmath STATIC)

target_sources(
  llmath
  PRIVATE
    alplaneset.cpp
    alprojection.cpp
    alsimdkernels.cpp
    llbbox.cpp
    llbboxlocal.cpp
    llcalc.cpp
    llcalcparser.cpp
    llcamera.cpp
    llcoordframe.cpp
    llline.cpp
    llmatrix3a.cpp
    llmatrix4a.cpp
    llmeshoptimizer.cpp
    llmodularmath.cpp
    lloctree.cpp
    llquaternion.cpp
    llrigginginfo.cpp
    llrect.cpp
    llvector4a.cpp
    llvolume.cpp
    llvolumemgr.cpp
    llvolumeoctree.cpp
    llsdutil_math.cpp
    m3math.cpp
    m4math.cpp
    raytrace.cpp
    v2math.cpp
    v3color.cpp
    v3dmath.cpp
    v3math.cpp
    v4color.cpp
    v4coloru.cpp
    v4math.cpp
    xform.cpp
)

target_sources(
  llmath
  PUBLIC
    FILE_SET HEADERS
      FILES
        alplaneset.h
        alprojection.h
        alsimdkernels.h
        alsimdkernels.inl
        llbbox.h
        llbboxlocal.h
        llcalc.h
        llcalcparser.h
        llcamera.h
        llcoord.h
        llcoordframe.h
        llinterp.h
        llline.h
        llmath.h
        llmatrix3a.h
        llmatrix3a.inl
        llmatrix4a.h
        llmeshoptimizer.h
        llmodularmath.h
        lloctree.h
        llplane.h
        llquantize.h
        llquaternion.h
        llquaternion2.h
        llquaternion2.inl
        llrect.h
        llrigginginfo.h
        llsimdmath.h
        llsimdtypes.h
        llsimdtypes.inl
        lltreenode.h
        llvector4a.h
        llvector4a.inl
        llvector4logical.h
        llvolume.h
        llvolumemgr.h
        llvolumeoctree.h
        llsdutil_math.h
        m3math.h
        m4math.h
        raytrace.h
        v2math.h
        v3color.h
        v3colorutil.h
        v3dmath.h
        v3math.h
        v4color.h
        v4coloru.h
        v4math.h
        xform.h
)

target_link_libraries(llmath PRIVATE al::flags PUBLIC llcommon ll::mikktspace ll::meshoptimizer)
target_precompile_headers(llmath REUSE_FROM llprecompiled)

# Add tests
if(AL_BUILD_TESTS)
  # One Catch2 binary for llmath's tests, each test file run in a process of
  # its own. Each links the library rather than compiling a copy of the file
  # it tests, so the code under test is the code the viewer runs.
  al_add_test_binary(
    llmath_tests
    PROJECT llmath
    SOURCES
      tests/alignment_test.cpp
      tests/alplaneset_test.cpp
      tests/alsimdkernels_test.cpp
      tests/llbbox_test.cpp
      tests/llbboxlocal_test.cpp
      tests/llmatrix3a_test.cpp
      tests/llmatrix4a_test.cpp
      tests/llmodularmath_test.cpp
      tests/llquaternion2_test.cpp
      tests/llquaternion_test.cpp
      tests/llrect_test.cpp
      tests/llsdutil_math_test.cpp
      tests/llvector4a_test.cpp
      tests/m3math_test.cpp
      tests/mathmisc_test.cpp
      tests/v2math_test.cpp
      tests/v3color_test.cpp
      tests/v3dmath_test.cpp
      tests/v3math_test.cpp
      tests/v4color_test.cpp
      tests/v4coloru_test.cpp
      tests/v4math_test.cpp
      tests/xform_test.cpp
    # Nanoseconds per element for the SIMD math the geometry rebuild spends
    # its time in, and for the choices the SIMD series makes by a number.
    BENCH_SOURCES tests/llsimd_bench.cpp
    LIBRARIES llmath llcommon
  )

  # llcommon's SIMD ops layer and the batch kernels again at each other
  # x86-64 tier, one binary a tier: code the linker folds across files would
  # otherwise run one tier's instructions in another's test. The tree's own
  # tier is llcommon_tests' and llmath_tests'; a variant the host cannot run
  # skips.
  if(BUILD_TARGET_IS_X86_64 AND NOT DARWIN)
    foreach(tier baseline v2 v3 v4)
      if(NOT tier STREQUAL AL_ISA_TIER)
        al_add_test_binary(
          simd_tests
          PROJECT llmath
          SOURCES tests/alsimdkernels_test.cpp ${INDRA_SOURCE_DIR}/llcommon/tests/alsimd_test.cpp
          LIBRARIES llmath llcommon
          ISA_TIER ${tier}
        )
      endif()
    endforeach()
  endif()
endif()
