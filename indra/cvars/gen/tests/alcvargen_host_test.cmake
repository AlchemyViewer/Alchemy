# -*- cmake -*-
#
# The generator's own build is for the machine the build runs on, whatever
# the build is for: a cross build's generator must run where it is built.
#
#   cmake -DINDRA_SOURCE_DIR=<indra> -P alcvargen_host_test.cmake

cmake_minimum_required(VERSION 4.0)

include("${INDRA_SOURCE_DIR}/cmake/ALCVarGen.cmake")

set(failures "")

# expect(<what> <generator> <platform> <host processor> <host is Apple> <expected args>...)
function(
  expect
  what
  generator
  platform
  processor
  apple
)
  set(CMAKE_GENERATOR "${generator}")
  set(CMAKE_GENERATOR_PLATFORM "${platform}")
  set(CMAKE_GENERATOR_TOOLSET "")
  set(CMAKE_HOST_SYSTEM_PROCESSOR "${processor}")
  set(CMAKE_HOST_APPLE ${apple})
  _al_cvargen_host_generator_args(args)
  if(NOT "${args}" STREQUAL "${ARGN}")
    set(failures "${failures}\n  ${what}: ${args}, not ${ARGN}" PARENT_SCOPE)
  endif()
endfunction()

expect(
  "Visual Studio for ARM64 on an x64 machine"
  "Visual Studio 18 2026" ARM64 AMD64 FALSE
  -G "Visual Studio 18 2026" -A x64
)
expect(
  "Visual Studio for x64 on an ARM64 machine"
  "Visual Studio 18 2026" x64 ARM64 FALSE
  -G "Visual Studio 18 2026" -A ARM64
)
expect(
  "Visual Studio for its own machine"
  "Visual Studio 18 2026" "" AMD64 FALSE
  -G "Visual Studio 18 2026" -A x64
)
expect(
  "Ninja for arm64 on an Intel Mac"
  "Ninja Multi-Config" "" x86_64 TRUE
  -G "Ninja Multi-Config" -DCMAKE_OSX_ARCHITECTURES=x86_64
)
expect(
  "Xcode for x86_64 on an Apple Silicon Mac"
  Xcode "" arm64 TRUE
  -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
)
expect(
  "Ninja on Linux"
  "Ninja Multi-Config" "" x86_64 FALSE
  -G "Ninja Multi-Config"
)

if(failures)
  message(FATAL_ERROR "alcvargen's own build is not the build machine's:${failures}")
endif()
message(STATUS "alcvargen's own build is the build machine's in every case")
