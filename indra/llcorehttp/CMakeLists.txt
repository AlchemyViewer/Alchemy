# -*- cmake -*-

add_library(llcorehttp STATIC)

target_sources(
  llcorehttp
  PRIVATE
    bufferarray.cpp
    bufferstream.cpp
    httpcommon.cpp
    llhttpconstants.cpp
    httpheaders.cpp
    httpoptions.cpp
    httprequest.cpp
    httpresponse.cpp
    httpstats.cpp
    llwebsocketmgr.cpp
    lljsonrpcws.cpp
    _httplibcurl.cpp
    _httpopcancel.cpp
    _httpoperation.cpp
    _httpoprequest.cpp
    _httpopsetget.cpp
    _httpopsetpriority.cpp
    _httppolicy.cpp
    _httppolicyclass.cpp
    _httppolicyglobal.cpp
    _httpreplyqueue.cpp
    _httprequestqueue.cpp
    _httpservice.cpp
    _refcounted.cpp
)

target_sources(
  llcorehttp
  PUBLIC
    FILE_SET HEADERS
      FILES
        bufferarray.h
        bufferstream.h
        httpcommon.h
        llhttpconstants.h
        httphandler.h
        httpheaders.h
        httpoptions.h
        httprequest.h
        httpresponse.h
        httpstats.h
        llwebsocketmgr.h
        lljsonrpcws.h
        _httpinternal.h
        _httplibcurl.h
        _httpopcancel.h
        _httpoperation.h
        _httpoprequest.h
        _httpopsetget.h
        _httpopsetpriority.h
        _httppolicy.h
        _httppolicyclass.h
        _httppolicyglobal.h
        _httpreadyqueue.h
        _httpreplyqueue.h
        _httprequestqueue.h
        _httpservice.h
        _mutex.h
        _refcounted.h
        _thread.h
)

target_link_libraries(llcorehttp PRIVATE al::flags PUBLIC llmath llcommon ll::libcurl ll::openssl)
# llmessage depends on llcorehttp, yet llcorehttp also depends on llmessage (at least for includes).
# Cannot/Should not use target_link_libraries here to add llmessage to the dependencies, as that would
# lead to circular dependencies (or in case of cmake, the first project declaring it's dependencies wins)
target_include_directories(llcorehttp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../llmessage)
target_precompile_headers(llcorehttp REUSE_FROM llprecompiled)

# tests
if(AL_BUILD_TESTS)
  # The state guard's checks on what llcorehttp keeps for the process, for
  # every test binary that links it.
  add_library(llcorehttp_test_checks OBJECT tests/llcorehttp_test_checks.cpp)
  target_link_libraries(llcorehttp_test_checks PRIVATE al::flags al_test_support llcorehttp llmessage)
  set_target_properties(llcorehttp_test_checks PROPERTIES FOLDER "Tests/llcorehttp")

  # The suites run under a Python peer that serves the requests they make.
  # An http_proxy in the environment would put a proxy between the two, so
  # the run clears it.
  al_add_test_binary(
    llcorehttp_tests
    PROJECT llcorehttp
    SOURCES
      tests/bufferarray_test.cpp
      tests/bufferstream_test.cpp
      tests/httpheaders_test.cpp
      tests/httpoperation_test.cpp
      tests/httprequest_test.cpp
      tests/httprequestqueue_test.cpp
      tests/httpstatus_test.cpp
      tests/jsonrpcws_test.cpp
      tests/refcounted_test.cpp
      tests/websocketmgr_test.cpp
    LIBRARIES llcorehttp llmessage llcommon
    SUPPORT llcorehttp_test_checks
    PYTHON
    LAUNCHER ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_llcorehttp_peer.py"
    ENVIRONMENT "http_proxy="
  )

  #
  # Example Programs
  #
  add_executable(http_texture_load examples/http_texture_load.cpp)
  set_target_properties(
    http_texture_load
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}" FOLDER "Tests/llcorehttp"
  )

  target_link_libraries(http_texture_load PRIVATE al::flags llcorehttp llmessage llcommon)

  if(WINDOWS)
    set_target_properties(http_texture_load PROPERTIES AL_SKIP_RELEASE_DEBUG_INFO ON)
  endif()
endif()
