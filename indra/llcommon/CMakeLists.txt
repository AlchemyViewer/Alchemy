
# -*- cmake -*-

project(llcommon LANGUAGES CXX)

include(00-Common)
include(CXXFeatureDetection)
include(GeneratePrecompiledHeader)
include(LLCommon)
include(LLDarwin)
include(LLMath)
include(Linking)
include(Boost)
include(OpenSSL)
include(LLSharedLibs)
include(GooglePerfTools)
include(Copy3rdPartyLibs)
include(ZLIB)
include(URIPARSER)

include_directories(
    ${EXPAT_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLDARWIN_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${BREAKPAD_INCLUDE_DIRECTORIES}
    ${URIPARSER_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    )

include_directories(
    SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    )

# add_executable(lltreeiterators lltreeiterators.cpp)
# 
# target_link_libraries(lltreeiterators
#     ${LLCOMMON_LIBRARIES})

set(llcommon_SOURCE_FILES
    indra_constants.cpp
    llallocator.cpp
    llallocator_heap_profile.cpp
    llapp.cpp
    llapr.cpp
    llassettype.cpp
    llatomic.cpp
    llbase32.cpp
    llbase64.cpp
    llbitpack.cpp
    llcallbacklist.cpp
    llcallstack.cpp
    llcleanup.cpp
    llcommon.cpp
    llcommonutils.cpp
    llcoro_get_id.cpp
    llcoros.cpp
    llcrc.cpp
    llcriticaldamp.cpp
    llcurrencywrapper.cpp
    lldate.cpp
    lldeadmantimer.cpp
    lldependencies.cpp
    lldictionary.cpp
    llerror.cpp
    llerrorthread.cpp
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
    llleap.cpp
    llleaplistener.cpp
    llliveappconfig.cpp
    lllivefile.cpp
    llmd5.cpp
    llmemory.cpp
    llmemorystream.cpp
    llmetrics.cpp
    llmetricperformancetester.cpp
    llmortician.cpp
    llmutex.cpp
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
    llstacktrace.cpp
    llstreamqueue.cpp
    llstreamtools.cpp
    llstring.cpp
    llstringtable.cpp
    llsys.cpp
    llthread.cpp
    llthreadlocalstorage.cpp
    llthreadsafequeue.cpp
    lltimer.cpp
    lltrace.cpp
    lltraceaccumulators.cpp
    lltracerecording.cpp
    lltracethreadrecorder.cpp
    lluri.cpp
    lluriparser.cpp
    lluuid.cpp
    llworkerthread.cpp
    timing.cpp
    StackWalker.cpp
    )
    
set(llcommon_HEADER_FILES
    CMakeLists.txt

    fix_macros.h
    indra_constants.h
    is_approx_equal_fraction.h
    linden_common.h
    llalignedarray.h
    llallocator.h
    llallocator_heap_profile.h
    llapp.h
    llapr.h
    llassettype.h
    llatomic.h
    llbase32.h
    llbase64.h
    llbitpack.h
    llboost.h
    llcallbacklist.h
    llcallstack.h
    llcleanup.h
    llcommon.h
    llcommonprecompiled.h
    llcommonutils.h
    llcoro_get_id.h
    llcoros.h
    llcrc.h
    llcriticaldamp.h
    llcurrencywrapper.h
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
    llerrorthread.h
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
    llheartbeat.h
    llheteromap.h
    llindexedvector.h
    llinitdestroyclass.h
    llinitparam.h
    llinstancetracker.h
    llkeythrottle.h
    llleap.h
    llleaplistener.h
    lllistenerwrapper.h
    llliveappconfig.h
    lllivefile.h
    llmd5.h
    llmemory.h
    llmemorystream.h
    llmetrics.h
    llmetricperformancetester.h
    llmortician.h
    llmutex.h
    llnametable.h
    llpointer.h
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
    llrefcount.h
    llsafehandle.h
    llsd.h
    llsdjson.h
    llsdparam.h
    llsdserialize.h
    llsdserialize_xml.h
    llsdutil.h
    llsimplehash.h
    llsingleton.h
    llsortedvector.h
    llstacktrace.h
    llstl.h
    llstreamqueue.h
    llstreamtools.h
    llstrider.h
    llstring.h
    llstringtable.h
    llstaticstringtable.h
    llstatsaccumulator.h
    llsys.h
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
    llworkerthread.h
    stdtypes.h
    stringize.h
    timer.h
    StackWalker.h
    )

if (WINDOWS)
  list(APPEND llcommon_HEADER_FILES llwin32headers.h llwin32headerslean.h)
endif (WINDOWS)

set_source_files_properties(${llcommon_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llcommon_SOURCE_FILES ${llcommon_HEADER_FILES})

add_library (llcommon ${llcommon_SOURCE_FILES})

if(USE_PRECOMPILED_HEADERS)
  target_precompiled_header(llcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/llcommonprecompiled.h
    ${CMAKE_CURRENT_SOURCE_DIR}/llcommonprecompiled.cpp)
endif(USE_PRECOMPILED_HEADERS)

set_target_properties(llcommon PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_features(llcommon PRIVATE cxx_nullptr cxx_range_for)
target_compile_features(llcommon PUBLIC cxx_decltype cxx_nullptr cxx_static_assert cxx_thread_local cxx_rvalue_references)

target_link_libraries(
    llcommon
    PUBLIC
    ${LLDARWIN_LIBRARIES}
    ${APRUTIL_LIBRARIES}
    ${APR_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${WINDOWS_LIBRARIES}
    ${BOOST_COROUTINE_LIBRARY}
    ${BOOST_CONTEXT_LIBRARY}
    ${BOOST_PROGRAM_OPTIONS_LIBRARY}
    ${BOOST_REGEX_LIBRARY}
    ${BOOST_CHRONO_LIBRARY}
    ${BOOST_THREAD_LIBRARY}
    ${BOOST_SYSTEM_LIBRARY}
    ${GOOGLE_PERFTOOLS_LIBRARIES}
    ${URIPARSER_LIBRARY}
    absl::flat_hash_map
    absl::node_hash_map
    fmt::fmt
    nlohmann_json::nlohmann_json
    ${RT_LIBRARY}
    )

add_dependencies(llcommon stage_third_party_libs)

if (LL_TESTS)
  include(LLAddBuildTest)
  SET(llcommon_TEST_SOURCE_FILES
    # unit-testing llcommon is not possible right now as the test-harness *itself* depends upon llcommon, causing a circular dependency.  Add your 'unit' tests as integration tests for now.
    )
  LL_ADD_PROJECT_UNIT_TESTS(llcommon "${llcommon_TEST_SOURCE_FILES}")

  #set(TEST_DEBUG on)
  set(test_libs llcommon 
      ${LLCOMMON_LIBRARIES} 
      ${WINDOWS_LIBRARIES} 
      ${BOOST_COROUTINE_LIBRARY} 
      ${BOOST_CONTEXT_LIBRARY} 
      ${BOOST_THREAD_LIBRARY} 
      ${BOOST_SYSTEM_LIBRARY}
      ${OPENSSL_LIBRARIES}
      ${CRYPTO_LIBRARIES})
  LL_ADD_INTEGRATION_TEST(commonmisc "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(bitpack "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llbase64 "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lldate "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lldeadmantimer "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lldependencies "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llerror "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lleventdispatcher "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lleventcoro "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lleventfilter "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llframetimer "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llheteromap "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llinstancetracker "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llleap "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llpounceable "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llprocess "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llprocessor "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llprocinfo "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llrand "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llsdserialize "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llsingleton "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llstreamqueue "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llstring "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lltrace "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lltreeiterators "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(lluri "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(llunits "" "${test_libs}")
  LL_ADD_INTEGRATION_TEST(stringize "" "${test_libs}")

## llexception_test.cpp isn't a regression test, and doesn't need to be run
## every build. It's to help a developer make implementation choices about
## throwing and catching exceptions.
##LL_ADD_INTEGRATION_TEST(llexception "" "${test_libs}")

endif (LL_TESTS)
