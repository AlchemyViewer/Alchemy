# -*- cmake -*-

add_library (llcorehttp STATIC)

target_sources(llcorehttp
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

target_sources(llcorehttp
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

target_link_libraries(
        llcorehttp
        PRIVATE
        al::flags
        PUBLIC
        llmath
        llcommon
        ll::libcurl
        ll::openssl
        ll::websocketpp
  )
# llmessage depends on llcorehttp, yet llcorehttp also depends on llmessage (at least for includes).
# Cannot/Should not use target_link_libraries here to add llmessage to the dependencies, as that would
# lead to circular dependencies (or in case of cmake, the first project declaring it's dependencies wins)
target_include_directories(llcorehttp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../llmessage)
target_precompile_headers(llcorehttp REUSE_FROM llprecompiled)

# tests
option(LLCOREHTTP_TESTS "Build and run llcorehttp integration tests specifically" ON)
if (AL_BUILD_TESTS AND LLCOREHTTP_TESTS)
  SET(llcorehttp_TEST_SOURCE_FILES
      )

  set(llcorehttp_TEST_HEADER_FILES
      tests/test_httpstatus.hpp
      tests/test_refcounted.hpp
      tests/test_httpoperation.hpp
      tests/test_httprequest.hpp
      tests/test_httprequestqueue.hpp
      tests/test_httpheaders.hpp
      tests/test_bufferarray.hpp
      tests/test_bufferstream.hpp
      tests/test_jsonrpcws.hpp
      )

  list(APPEND llcorehttp_TEST_SOURCE_FILES ${llcorehttp_TEST_HEADER_FILES})

  # LL_ADD_PROJECT_UNIT_TESTS(llcorehttp "${llcorehttp_TEST_SOURCE_FILES}")

  #    set(TEST_DEBUG on)
  set(test_libs
          llcorehttp
          llmessage
          llcommon
      )
  set(test_project llcorehttp)

  # If http_proxy is in the current environment (e.g. to fetch s3-proxy
  # autobuild packages), suppress it for this integration test: it screws up
  # the tests.
  LL_ADD_INTEGRATION_TEST(llcorehttp
                          "${llcorehttp_TEST_SOURCE_FILES}"
                          "${test_libs}"
                          "${test_project}"
                          "-Dhttp_proxy"
                          ${Python3_EXECUTABLE}
                          "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_llcorehttp_peer.py"
                          )

  #
  # Example Programs
  #
  add_executable(http_texture_load examples/http_texture_load.cpp)
  set_target_properties(http_texture_load
                        PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
                        FOLDER "Tests/llcorehttp"
                        )

  target_link_libraries(http_texture_load PRIVATE al::flags llcorehttp llmessage llcommon)

  if (WINDOWS)
    set_target_properties(http_texture_load PROPERTIES AL_SKIP_RELEASE_DEBUG_INFO ON)
  endif()

endif (AL_BUILD_TESTS AND LLCOREHTTP_TESTS)
