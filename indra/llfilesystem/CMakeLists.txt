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

if(AL_USE_VELOPACK)
  # A Velopack portable install keeps its user data beside the install
  # (LLDir::isPortableInstall). Only the directory setup reads the define, so
  # only its sources take it, and build without the shared PCH, which was
  # built without it.
  set(llfilesystem_velopack_sources lldir.cpp)
  if(WINDOWS)
    list(APPEND llfilesystem_velopack_sources lldir_win32.cpp)
  elseif(DARWIN)
    list(APPEND llfilesystem_velopack_sources lldir_mac.mm)
  endif()
  set_source_files_properties(
    ${llfilesystem_velopack_sources}
    PROPERTIES COMPILE_DEFINITIONS LL_VELOPACK=1 SKIP_PRECOMPILE_HEADERS TRUE
  )
endif()

# Add tests
if(AL_BUILD_TESTS)
  al_add_test_binary(
    llfilesystem_tests
    PROJECT llfilesystem
    SOURCES tests/lldir_test.cpp tests/lldiriterator_test.cpp
    LIBRARIES llfilesystem llmath llcommon
  )
endif()
