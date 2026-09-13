# -*- cmake -*-

add_library(llfilesystem STATIC)

target_sources(
  llfilesystem
  PRIVATE lldir.cpp lldiriterator.cpp lllfsthread.cpp lldiskcache.cpp llfilesystem.cpp
)

target_sources(
  llfilesystem
  PUBLIC
    FILE_SET HEADERS
      FILES lldir.h lldirguard.h lldiriterator.h lllfsthread.h lldiskcache.h llfilesystem.h
)

if(DARWIN)
  target_sources(llfilesystem PRIVATE lldir_mac.mm PUBLIC FILE_SET HEADERS FILES lldir_mac.h)
  set_source_files_properties(lldir_mac.mm PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif()

if(LINUX)
  target_sources(llfilesystem PRIVATE lldir_linux.cpp PUBLIC FILE_SET HEADERS FILES lldir_linux.h)
endif()

if(WINDOWS)
  target_sources(llfilesystem PRIVATE lldir_win32.cpp PUBLIC FILE_SET HEADERS FILES lldir_win32.h)
endif()

target_link_libraries(llfilesystem PRIVATE al::flags PUBLIC llmath llcommon)
target_precompile_headers(llfilesystem REUSE_FROM llprecompiled)

# Add tests
if(AL_BUILD_TESTS)
  al_add_test(lldiriterator UNIT PROJECT llfilesystem)
  al_add_test(lldir PROJECT llfilesystem LIBRARIES llmath llcommon llfilesystem)
endif()
