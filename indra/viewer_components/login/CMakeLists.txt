# -*- cmake -*-

add_library(lllogin STATIC)

target_sources(lllogin PRIVATE lllogin.cpp PUBLIC FILE_SET HEADERS FILES lllogin.h)

target_link_libraries(
  lllogin
  PRIVATE al::flags
  PUBLIC llmessage llcorehttp llcommon llmath llxml
)

if(AL_BUILD_TESTS)
  al_add_test(lllogin UNIT PROJECT lllogin LIBRARIES llmessage llcorehttp llcommon)
endif()
