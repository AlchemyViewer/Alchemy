# -*- cmake -*-

# SQLite database files, for what the viewer keeps: settings first, then the
# cache's index and the chat logs. One writer thread a file, whose jobs are
# written in batches, each in a savepoint of its own; a pool of readers;
# migrations, with a copy kept before each; backups; and a damaged file moved
# aside and salvaged with SQLite's recover API.

set(aldb_SOURCES aldatabase.cpp aldbconnection.cpp aldbrecovery.cpp aldbwriter.cpp)

set(aldb_HEADERS aldatabase.h aldbconnection.h aldbrecovery.h aldbwriter.h)

add_library(aldb STATIC)
target_sources(aldb PRIVATE ${aldb_SOURCES} PUBLIC FILE_SET HEADERS FILES ${aldb_HEADERS})

target_link_libraries(aldb PRIVATE al::flags PUBLIC llcommon ll::sqlite3)

target_precompile_headers(aldb REUSE_FROM llprecompiled)

if(AL_BUILD_TESTS)
  # A VFS over the platform's that fails or holds writes, or offers no shared
  # memory, on request: for the tests of what the writer does when the disk
  # does.
  add_library(aldb_test_support OBJECT tests/aldbfaultvfs.cpp)
  target_link_libraries(aldb_test_support PRIVATE al::flags al_test_support aldb)
  set_target_properties(aldb_test_support PROPERTIES FOLDER "Tests/aldb")

  al_add_test_binary(
    aldb_tests
    PROJECT aldb
    SOURCES tests/aldatabase_test.cpp tests/aldbconnection_test.cpp tests/aldbrecovery_test.cpp
    LIBRARIES aldb
    SUPPORT aldb_test_support llcommon_test_checks
  )
endif()
