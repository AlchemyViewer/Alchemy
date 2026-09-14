# -*- cmake -*-

add_library(llmath STATIC)

target_sources(
  llmath
  PRIVATE
    alplaneset.cpp
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
    llperlin.cpp
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
        llperlin.h
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
  foreach(
    test
    llbboxlocal
    llmodularmath
    llrect
    v2math
    v3color
    v4color
    v4coloru
  )
    al_add_test(${test} UNIT PROJECT llmath)
  endforeach()

  foreach(
    test
    alignment
    alplaneset
    llsdutil_math
    mathmisc
    m3math
    llmatrix3a
    llmatrix4a
    llvector4a
  )
    al_add_test(${test} PROJECT llmath LIBRARIES llmath llcommon)
  endforeach()
  al_add_test(llbbox PROJECT llmath SOURCES llbbox.cpp LIBRARIES llmath llcommon)
  al_add_test(llquaternion PROJECT llmath SOURCES llquaternion.cpp LIBRARIES llmath llcommon)
  al_add_test(llquaternion2 PROJECT llmath SOURCES llquaternion.cpp LIBRARIES llmath llcommon)
  al_add_test(v3dmath PROJECT llmath SOURCES v3dmath.cpp LIBRARIES llmath llcommon)
  al_add_test(v3math PROJECT llmath SOURCES v3math.cpp LIBRARIES llmath llcommon)
  al_add_test(v4math PROJECT llmath SOURCES v4math.cpp LIBRARIES llmath llcommon)
  al_add_test(xform PROJECT llmath SOURCES xform.cpp LIBRARIES llmath llcommon)

  # The batch kernels at every width, at the tree's tier and, where the
  # tiers differ, at each of them; a variant the host cannot run skips.
  al_add_test(alsimdkernels PROJECT llmath LIBRARIES llmath llcommon)
  if(BUILD_TARGET_IS_X86_64 AND NOT DARWIN)
    foreach(tier baseline v2 v3 v4)
      al_add_test(alsimdkernels PROJECT llmath LIBRARIES llmath llcommon ISA_TIER ${tier})
    endforeach()
  endif()

  # Nanoseconds per element for the SIMD math the geometry rebuild spends
  # its time in, and for the choices the SIMD series makes by a number.
  al_add_bench(llsimd PROJECT llmath LIBRARIES llmath llcommon)
endif()
