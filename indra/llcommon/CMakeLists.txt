# -*- cmake -*-

if(AL_BUILD_SHARED_LLCOMMON)
  add_library(llcommon SHARED)

  target_compile_definitions(llcommon PUBLIC LL_COMMON_LINK_SHARED=1)
  set_target_properties(
    llcommon
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${SHARED_LIB_STAGING_DIR}"
      LIBRARY_OUTPUT_DIRECTORY "${SHARED_LIB_STAGING_DIR}"
  )
else()
  add_library(llcommon STATIC)
endif()

target_sources(
  llcommon
  PRIVATE
    alparamtype.cpp
    apply.cpp
    commoncontrol.cpp
    indra_constants.cpp
    lazyeventapi.cpp
    llapp.cpp
    llapr.cpp
    llassettype.cpp
    llatomic.cpp
    llbase64.cpp
    llbitpack.cpp
    llcallbacklist.cpp
    llcleanup.cpp
    llcommon.cpp
    llcommonutils.cpp
    llcoros.cpp
    llcrc.cpp
    llcriticaldamp.cpp
    lldate.cpp
    lldeadmantimer.cpp
    lldependencies.cpp
    lldictionary.cpp
    llerror.cpp
    llevent.cpp
    lleventapi.cpp
    lleventcoro.cpp
    lleventdispatcher.cpp
    lleventfilter.cpp
    llevents.cpp
    lleventtimer.cpp
    llexception.cpp
    llfasttimer.cpp
    llfile.cpp
    llfindlocale.cpp
    llfixedbuffer.cpp
    llformat.cpp
    llframetimer.cpp
    llheartbeat.cpp
    llheteromap.cpp
    llinitparam.cpp
    llinitdestroyclass.cpp
    llinstancetracker.cpp
    llkeybind.cpp
    llleap.cpp
    llleaplistener.cpp
    llliveappconfig.cpp
    lllivefile.cpp
    llmd5.cpp
    llmemory.cpp
    llmemorystream.cpp
    llmetrics.cpp
    llmortician.cpp
    llmutex.cpp
    llpointer.cpp
    llptrto.cpp
    llpredicate.cpp
    llprocess.cpp
    llprocessor.cpp
    llprocinfo.cpp
    llqueuedthread.cpp
    llrand.cpp
    llrefcount.cpp
    llrun.cpp
    llsd.cpp
    llsdjson.cpp
    llsdparam.cpp
    llsdserialize.cpp
    llsdserialize_xml.cpp
    llsdutil.cpp
    llsingleton.cpp
    llstreamtools.cpp
    llstring.cpp
    llstringtable.cpp
    llsys.cpp
    lltempredirect.cpp
    llthread.cpp
    llthreadsafequeue.cpp
    lltimer.cpp
    lltrace.cpp
    lltraceaccumulators.cpp
    lltracerecording.cpp
    lltracethreadrecorder.cpp
    lluri.cpp
    lluriparser.cpp
    lluuid.cpp
    llwatchdog.cpp
    llworkerthread.cpp
    hbxxh.cpp
    u64.cpp
    threadpool.cpp
    workqueue.cpp
)

target_sources(
  llcommon
  PUBLIC
    FILE_SET HEADERS
      FILES
        alparamtype.h
        alsimd.h
        always_return.h
        apply.h
        chrono.h
        classic_callback.h
        commoncontrol.h
        fsyspath.h
        function_types.h
        indra_constants.h
        lazyeventapi.h
        linden_common.h
        llalignedarray.h
        llapp.h
        llapr.h
        llassettype.h
        llatomic.h
        llbase64.h
        llbitpack.h
        llboost.h
        llcallbacklist.h
        llcleanup.h
        llcommon.h
        llcommonutils.h
        llcond.h
        llcoros.h
        llcrc.h
        llcriticaldamp.h
        lldate.h
        lldeadmantimer.h
        lldefs.h
        lldependencies.h
        lldepthstack.h
        lldictionary.h
        lldoubledispatch.h
        llendianswizzle.h
        llerror.h
        llerrorcontrol.h
        llevent.h
        lleventapi.h
        lleventcoro.h
        lleventdispatcher.h
        lleventfilter.h
        llevents.h
        lleventemitter.h
        llexception.h
        llfasttimer.h
        llfile.h
        llfindlocale.h
        llfixedbuffer.h
        llformat.h
        llframetimer.h
        llhandle.h
        llhash.h
        llheartbeat.h
        llheteromap.h
        llindexedvector.h
        llinitdestroyclass.h
        llinitparam.h
        llinstancetracker.h
        llinstancetrackersubclass.h
        llkeybind.h
        llkeythrottle.h
        llleap.h
        llleaplistener.h
        llliveappconfig.h
        lllivefile.h
        llmainthreadtask.h
        llmd5.h
        llmemory.h
        llmemorystream.h
        llmetrics.h
        llmortician.h
        llmutex.h
        llnametable.h
        llpointer.h
        llprofiler.h
        llprofilercategories.h
        llpounceable.h
        llpredicate.h
        llpreprocessor.h
        llpriqueuemap.h
        llprocess.h
        llprocessor.h
        llprocinfo.h
        llptrto.h
        llqueuedthread.h
        llrand.h
        llrefcount.h
        llregex.h
        llregistry.h
        llrun.h
        llsafehandle.h
        llsd.h
        llsdjson.h
        llsdparam.h
        llsdserialize.h
        llsdserialize_xml.h
        llsdutil.h
        llsimplehash.h
        llsingleton.h
        llstl.h
        llstreamtools.h
        llstrider.h
        llstring.h
        llstringtable.h
        llstaticstringtable.h
        llstatsaccumulator.h
        llsys.h
        lltempredirect.h
        llthread.h
        llthreadlocalstorage.h
        llthreadsafequeue.h
        lltimer.h
        lltrace.h
        lltraceaccumulators.h
        lltracerecording.h
        lltracethreadrecorder.h
        lltreeiterators.h
        llunits.h
        llunittype.h
        lluri.h
        lluriparser.h
        lluuid.h
        llwatchdog.h
        llwin32headers.h
        llworkerthread.h
        hbxxh.h
        lockstatic.h
        stdtypes.h
        stringize.h
        threadpool.h
        threadpool_fwd.h
        threadsafeschedule.h
        tuple.h
        u64.h
        workqueue.h
)

if(DARWIN)
  target_sources(llcommon PRIVATE llsys_objc.mm PUBLIC FILE_SET HEADERS FILES llsys_objc.h)

  set_source_files_properties(llsys_objc.mm PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif()

if(AL_USE_TRACY)
  # Skip precompiled headers on these files due to special compilation needs
  set_source_files_properties(llfasttimer.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif()

target_link_libraries(
  llcommon
  PRIVATE al::flags
  PUBLIC
    ll::apr
    ll::zlib-ng
    ll::boost
    ll::oslibraries
    ll::tracy
    ll::xxhash
    ll::simdjson
    ll::simdutf
    ll::fmt
    Threads::Threads
  PRIVATE ll::fastfloat ll::icu
)

if(AL_BUILD_TESTS)
  # No unit tests: the harness itself links llcommon, so a test of one of its
  # files could not stand alone. Every test here links the library.
  foreach(
    test
    apply
    bitpack
    classic_callback
    commonmisc
    lazyeventapi
    llapp
    llbase64
    llcond
    lldate
    lldeadmantimer
    lldependencies
    lldoubledispatch
    llerror
    lleventcoro
    lleventdispatcher
    lleventfilter
    llevents
    llfile
    llframetimer
    llhttpdate
    llheteromap
    llinitparam
    llinstancetracker
    llmainthreadtask
    llmutex
    llpounceable
    llprocessor
    llprocinfo
    llrand
    llsd
    llsdjson
    llsdutil
    llsingleton
    llstreamtools
    llstring
    llstring_utf
    lltrace
    lltreeiterators
    llunits
    lluri
    lluuid
    stringize
    threadsafeschedule
    tuple
    workqueue
  )
    al_add_test(${test} PROJECT llcommon LIBRARIES llcommon)
  endforeach()

  # These drive a Python peer: LEAP's protocol is to an external script,
  # llprocess spawns one, and llsdserialize round-trips through the llsd
  # package as an independent implementation.
  foreach(test llleap llprocess llsdserialize)
    al_add_test(${test} PROJECT llcommon LIBRARIES llcommon PYTHON)
  endforeach()

  ## llexception_test.cpp isn't a regression test, and doesn't need to be run
  ## every build. It's to help a developer make implementation choices about
  ## throwing and catching exceptions.
  ##al_add_test(llexception PROJECT llcommon LIBRARIES llcommon)

  # The SIMD ops layer, at the tree's tier and, where the tiers differ, at
  # each of them; a variant the host cannot run skips.
  al_add_test(alsimd PROJECT llcommon LIBRARIES llcommon)
  if(BUILD_TARGET_IS_X86_64 AND NOT DARWIN)
    foreach(tier baseline v2 v3 v4)
      al_add_test(alsimd PROJECT llcommon LIBRARIES llcommon ISA_TIER ${tier})
    endforeach()
  endif()

  # The ops layer's assembly, one function per op, compiled with the tree's
  # flags and read by AlSimdAsmCheck.cmake for a table lookup, a call or a
  # function past its ceiling. Never under link-time optimisation, which
  # leaves the compiler nothing to list.
  add_library(alsimd_asm_probe OBJECT tests/alsimd_asm_probe.cpp)
  target_link_libraries(alsimd_asm_probe PRIVATE al::flags)
  set_target_properties(
    alsimd_asm_probe
    PROPERTIES
      FOLDER "Tests/llcommon"
      INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
      INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
  )
  if(COMPILER_IS_MSVC)
    target_compile_options(alsimd_asm_probe PRIVATE /FAs)
    set(alsimd_asm_syntax masm)
  elseif(COMPILER_IS_CLANG_CL)
    target_compile_options(alsimd_asm_probe PRIVATE /clang:-save-temps=obj)
    set(alsimd_asm_syntax gnu)
  else()
    target_compile_options(alsimd_asm_probe PRIVATE -save-temps=obj)
    set(alsimd_asm_syntax gnu)
  endif()
  if(BUILD_TARGET_IS_ARM64)
    set(alsimd_asm_arch arm64)
  else()
    set(alsimd_asm_arch x86)
  endif()
  add_test(
    NAME alsimd_asm_probe
    COMMAND
      ${CMAKE_COMMAND} -DOBJECT=$<TARGET_OBJECTS:alsimd_asm_probe> -DSYNTAX=${alsimd_asm_syntax}
      -DARCH=${alsimd_asm_arch} -DCOMPILER=${CMAKE_CXX_COMPILER_ID} -P
      ${INDRA_SOURCE_DIR}/cmake/AlSimdAsmCheck.cmake
  )
  add_dependencies(BUILD_TESTS alsimd_asm_probe)

  # No source outside the ops layer and the kernels names an intrinsic, so
  # nothing compiles on one architecture only.
  add_test(
    NAME alsimd_intrinsic_check
    COMMAND
      ${CMAKE_COMMAND} -DSOURCE_DIR=${INDRA_SOURCE_DIR} -P
      ${INDRA_SOURCE_DIR}/cmake/AlIntrinsicCheck.cmake
  )

  # Skip precompiled header for tests that need special handling
  set_source_files_properties(tests/llsd_test.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif()
