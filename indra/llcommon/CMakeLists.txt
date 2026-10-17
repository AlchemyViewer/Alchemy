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
    alfilewrite.cpp
    almainloop.cpp
    alserialworker.cpp
    alparamtype.cpp
    alrecoverystore.cpp
    alregex.cpp
    alsanitizer.cpp
    alsavehistory.cpp
    alwatchedfile.cpp
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
    llerror.cpp
    llevent.cpp
    lleventapi.cpp
    lleventcoro.cpp
    lleventdispatcher.cpp
    lleventfilter.cpp
    llevents.cpp
    lleventtimer.cpp
    llexception.cpp
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
        alenumtable.h
        alfilewrite.h
        allinebreaks.h
        almainloop.h
        alserialworker.h
        alparamtype.h
        alrandmap.h
        alrecoverystore.h
        alregex.h
        alsanitizer.h
        alsavehistory.h
        alsimd.h
        alwatchedfile.h
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
  PRIVATE ll::fastfloat ll::icu ll::re2
)

if(AL_BUILD_TESTS)
  # The state guard's checks on what llcommon keeps for the process, and the
  # event pumps' end after every case, in every binary of llcommon's tests
  # that shares a process between files.
  add_library(llcommon_test_checks OBJECT tests/llcommon_test_checks.cpp)
  target_link_libraries(llcommon_test_checks PRIVATE al::flags al_test_support llcommon)
  set_target_properties(llcommon_test_checks PROPERTIES FOLDER "Tests/llcommon")

  # One Catch2 binary for llcommon's tests, each test file run in a process
  # of its own. None of them may delete every singleton: the files will
  # share a process.
  al_add_test_binary(
    llcommon_tests
    PROJECT llcommon
    SOURCES
      tests/alenumtable_test.cpp
      tests/alfilewrite_test.cpp
      tests/alrecoverystore_test.cpp
      tests/alregex_test.cpp
      tests/alsavehistory_test.cpp
      tests/alserialworker_test.cpp
      tests/alsimd_test.cpp
      tests/alwatchedfile_test.cpp
      tests/apply_test.cpp
      tests/bitpack_test.cpp
      tests/classic_callback_test.cpp
      tests/commonmisc_test.cpp
      tests/lazyeventapi_test.cpp
      tests/llassettype_test.cpp
      tests/llbase64_test.cpp
      tests/llcallbacklist_test.cpp
      tests/llcond_test.cpp
      tests/lldate_test.cpp
      tests/lldeadmantimer_test.cpp
      tests/lldependencies_test.cpp
      tests/lldoubledispatch_test.cpp
      tests/llerror_test.cpp
      tests/lleventcoro_test.cpp
      tests/lleventdispatcher_test.cpp
      tests/lleventfilter_test.cpp
      tests/llevents_test.cpp
      tests/llfile_test.cpp
      tests/llframetimer_test.cpp
      tests/llheteromap_test.cpp
      tests/llhttpdate_test.cpp
      tests/llinitparam_test.cpp
      tests/llinstancetracker_test.cpp
      tests/llmainthreadtask_test.cpp
      tests/llmutex_test.cpp
      tests/llpounceable_test.cpp
      tests/llprocessor_test.cpp
      tests/llprocinfo_test.cpp
      tests/llptrto_test.cpp
      tests/llrand_test.cpp
      tests/llsd_test.cpp
      tests/llsdjson_test.cpp
      tests/llsdserialize_test.cpp
      tests/llsdutil_test.cpp
      tests/llstreamtools_test.cpp
      tests/llstring_test.cpp
      tests/llstring_utf_test.cpp
      tests/llthread_test.cpp
      tests/lltimer_test.cpp
      tests/lltrace_test.cpp
      tests/lltreeiterators_test.cpp
      tests/llunits_test.cpp
      tests/lluri_test.cpp
      tests/lluuid_test.cpp
      tests/stringize_test.cpp
      tests/threadsafeschedule_test.cpp
      tests/tuple_test.cpp
      tests/workqueue_test.cpp
    # The viewer's own patterns under Boost.Regex and under RE2, side by
    # side, over the text each is run on; and the cost of its locks.
    BENCH_SOURCES tests/alregex_bench.cpp tests/llmutex_bench.cpp
    SUPPORT llcommon_test_checks
    LIBRARIES llcommon
  )

  # These drive a Python peer: LEAP's protocol is to an external script,
  # llprocess spawns one, and llsdserialize_python round-trips through the
  # llsd package as an independent implementation. A peer that imports a
  # script a test wrote to the temp folder would leave its bytecode there.
  al_add_test_binary(
    llcommon_python_tests
    PROJECT llcommon
    SOURCES tests/llleap_test.cpp tests/llprocess_test.cpp tests/llsdserialize_python_test.cpp
    SUPPORT llcommon_test_checks
    LIBRARIES llcommon
    PYTHON
    ENVIRONMENT PYTHONDONTWRITEBYTECODE=1
  )

  # Alone: llsingleton's tests delete every singleton, and llapp's make and
  # destroy the process's LLApp, by design.
  al_add_test_binary(
    llsingleton_test
    PROJECT llcommon
    SOURCES tests/llsingleton_test.cpp
    LIBRARIES llcommon
  )
  al_add_test_binary(llapp_test PROJECT llcommon SOURCES tests/llapp_test.cpp LIBRARIES llcommon)

  # The ops layer's assembly, one function per op, compiled with the tree's
  # flags and read by AlSimdAsmCheck.cmake for a table lookup, a call or a
  # function past its ceiling. Never under link-time optimisation, which
  # leaves the compiler nothing to list.
  # A sanitized tree's listing is the sanitizers' as much as the ops layer's:
  # their checks are calls and instructions past every ceiling, and the
  # unsanitized trees check the same source.
  if(NOT AL_SANITIZING)
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
      # Visual Studio points /Fa at the object's directory itself; Ninja leaves
      # it to cl, which writes the listing to the build tree's root.
      target_compile_options(
        alsimd_asm_probe
        PRIVATE /FAs "/Fa$<PATH:GET_PARENT_PATH,$<TARGET_OBJECTS:alsimd_asm_probe>>/"
      )
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
  endif()

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

  # GCC 15 at -O3 takes the weak_ptr that LLInstanceTrackerSubclass::getWeak()
  # copies from mSelf, scalarised, for one read before it is set -- a false
  # positive on test 1's queue.getWeak(), and under -Werror a failed build.
  if(COMPILER_IS_GCC)
    set_source_files_properties(
      tests/workqueue_test.cpp
      PROPERTIES COMPILE_OPTIONS -Wno-uninitialized
    )
  endif()
endif()
