# -*- cmake -*-
#
# Definitions of variables used throughout the Second Life build
# process.
#
# Platform variables:
#
#   DARWIN  - macOS
#   LINUX   - Linux
#   WINDOWS - Windows

include_guard()

# Location of scripts directory
set(SCRIPTS_DIR ${INDRA_SOURCE_DIR}/../scripts)

# Select arch based on requested target processor
string(TOLOWER ${CMAKE_SYSTEM_PROCESSOR} processor_lower)
set(BUILD_TARGET_IS_ARM64 OFF)
set(BUILD_TARGET_IS_X86_64 OFF)
if(processor_lower STREQUAL "arm64" OR processor_lower STREQUAL "aarch64")
  set(ARCH arm64)
  set(BUILD_TARGET_IS_ARM64 ON)
else()
  set(ARCH x86_64)
  set(BUILD_TARGET_IS_X86_64 ON)
endif()

# Only 64-bit architectures are supported
set(ADDRESS_SIZE 64)

# Determine build platform
set(WINDOWS OFF)
set(LINUX OFF)
set(DARWIN OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(WINDOWS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(DARWIN ON)
endif()

set(COMPILER_IS_MSVC OFF)
set(COMPILER_IS_CLANG_CL OFF)
set(COMPILER_IS_CLANG OFF)
set(COMPILER_IS_GCC OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(COMPILER_IS_MSVC ON)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(COMPILER_IS_CLANG_CL ON)
  else()
    set(COMPILER_IS_CLANG ON)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(COMPILER_IS_GCC ON)
endif()

# Internal flags
string(REPLACE " " "" AL_CHANNEL_ONEWORD ${AL_CHANNEL})
