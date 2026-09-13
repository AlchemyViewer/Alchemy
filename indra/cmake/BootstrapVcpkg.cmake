# -*- cmake -*-
#
# The vcpkg toolchain and the triplet, decided before project(): vcpkg
# installs the manifest's ports inside the first project() call.

include_guard(GLOBAL)

# The toolchain: the vcpkg submodule beside the source, bootstrapped on first
# use. Set CMAKE_TOOLCHAIN_FILE yourself to use another vcpkg.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/../vcpkg")
  if(NOT EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR "vcpkg is not checked out at ${VCPKG_ROOT}. It is a submodule: "
      "run 'git submodule update --init vcpkg' in the repository root, "
      "or set CMAKE_TOOLCHAIN_FILE to another vcpkg's scripts/buildsystems/vcpkg.cmake.")
  endif()
  set(ENV{VCPKG_ROOT} ${VCPKG_ROOT})

  if(WIN32)
    set(VCPKG_EXECUTABLE ${VCPKG_ROOT}/vcpkg.exe)
    set(VCPKG_BOOTSTRAP ${VCPKG_ROOT}/bootstrap-vcpkg.bat)
  else()
    set(VCPKG_EXECUTABLE ${VCPKG_ROOT}/vcpkg)
    set(VCPKG_BOOTSTRAP ${VCPKG_ROOT}/bootstrap-vcpkg.sh)
  endif()

  if(NOT EXISTS "${VCPKG_EXECUTABLE}")
    message(STATUS "Bootstrapping vcpkg in ${VCPKG_ROOT}")
    execute_process(COMMAND "${VCPKG_BOOTSTRAP}" WORKING_DIRECTORY "${VCPKG_ROOT}" RESULT_VARIABLE bootstrap_result)
    if(bootstrap_result OR NOT EXISTS "${VCPKG_EXECUTABLE}")
      message(FATAL_ERROR "Could not bootstrap vcpkg: ${VCPKG_BOOTSTRAP} failed (${bootstrap_result})")
    endif()
  endif()

  set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake CACHE STRING "")
endif()

# The triplet, unless the caller named one: <arch>-<os>-alchemy[<tier>][-release].
# The tier follows AL_ISA_TIER on the platforms that build ports per tier, and
# a single-configuration tree that is not Debug takes the release-only ports.
get_property(LL_GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
  include(${CMAKE_CURRENT_LIST_DIR}/AlchemyTarget.cmake)

  if(WIN32)
    set(triplet_system Windows)
    set(triplet_os windows)
    set(triplet_arch x64)
  elseif(APPLE)
    set(triplet_system Darwin)
    set(triplet_os osx)
    if(DEFINED CMAKE_OSX_ARCHITECTURES)
      set(triplet_arch "${CMAKE_OSX_ARCHITECTURES}")
    else()
      cmake_host_system_information(RESULT triplet_arch QUERY OS_PLATFORM)
    endif()
    if(NOT triplet_arch STREQUAL "arm64")
      set(triplet_arch x64)
    endif()
  else()
    set(triplet_system Linux)
    set(triplet_os linux)
    set(triplet_arch x64)
  endif()

  al_isa_triplet_tier("${AL_ISA_TIER}" "${triplet_system}" triplet_tier)

  set(triplet_release "")
  if(NOT LL_GENERATOR_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(triplet_release "-release")
  endif()

  set(VCPKG_TARGET_TRIPLET "${triplet_arch}-${triplet_os}-alchemy${triplet_tier}${triplet_release}")
endif()
