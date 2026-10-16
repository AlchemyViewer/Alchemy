# -*- cmake -*-

add_library(lllogin STATIC)

target_sources(lllogin PRIVATE lllogin.cpp PUBLIC FILE_SET HEADERS FILES lllogin.h)

target_link_libraries(
  lllogin
  PRIVATE al::flags
  PUBLIC llmessage llcorehttp llcommon llmath llxml
)

if(AL_BUILD_TESTS)
  # Alone: each case makes an LLTestApp, of which a process may have one at a
  # time, and its end clears every event pump in the process.
  al_add_test_binary(lllogin_test PROJECT lllogin SOURCES tests/lllogin_test.cpp LIBRARIES lllogin)
endif()
