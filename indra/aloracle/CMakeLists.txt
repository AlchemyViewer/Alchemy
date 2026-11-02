# -*- cmake -*-

# aloracle: one shared library of C functions over the viewer's own C++
# (aloracle.h). A dev tool, built only with AL_BUILD_ORACLE, and never staged
# or packaged with the viewer.
# The libraries it wraps link in statically, and what it exports is
# aloracle.h's functions, and on Windows the data symbol ICU's library
# marks for export. It stands alone, so vcpkg's toolchain is kept from
# hanging its copy of dependent libraries beside it on the target.
set(VCPKG_APPLOCAL_DEPS OFF)
add_library(aloracle SHARED)

target_sources(
  aloracle
  PRIVATE aloracle.cpp aloracleboundary.h
  PUBLIC FILE_SET HEADERS FILES aloracle.h
)
set_target_properties(aloracle PROPERTIES DEFINE_SYMBOL ALORACLE_BUILDING)
target_link_libraries(aloracle PRIVATE al::flags llcommon)

if(LINUX)
  # The ports are built with their symbols visible, and would be exported
  # from the library beside its own.
  target_link_options(aloracle PRIVATE "LINKER:--exclude-libs,ALL")
endif()

# Each library the oracle covers: its wrappers, and the library they link,
# but llcommon, which the boundary in aloracle.cpp links already.

# llcommon: LLSD's formats, conversions and zipped blocks, LLURI's escaping and
# queries, and LLDate.
target_sources(aloracle PRIVATE aloracle_llsd.cpp aloracle_lluri.cpp aloracle_lldate.cpp)

# llmath: llquantize.h's quantized floats.
target_sources(aloracle PRIVATE aloracle_llquantize.cpp)
target_link_libraries(aloracle PRIVATE llmath)

# llmessage: zerocoding, and template messages read and written back.
target_sources(aloracle PRIVATE aloracle_llmessage.cpp)
target_link_libraries(aloracle PRIVATE llmessage)

if(AL_BUILD_TESTS)
  # The tests call the library through aloracle.h alone.
  al_add_test_binary(
    aloracle_tests
    PROJECT aloracle
    SOURCES tests/aloracle_test.cpp
    LIBRARIES aloracle
  )
endif()
