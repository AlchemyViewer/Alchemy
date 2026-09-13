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
    message(
      FATAL_ERROR
      "vcpkg is not checked out at ${VCPKG_ROOT}. It is a submodule: "
      "run 'git submodule update --init vcpkg' in the repository root, "
      "or set CMAKE_TOOLCHAIN_FILE to another vcpkg's scripts/buildsystems/vcpkg.cmake."
    )
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
    execute_process(
      COMMAND "${VCPKG_BOOTSTRAP}"
      WORKING_DIRECTORY "${VCPKG_ROOT}"
      RESULT_VARIABLE bootstrap_result
    )
    if(bootstrap_result OR NOT EXISTS "${VCPKG_EXECUTABLE}")
      message(
        FATAL_ERROR
        "Could not bootstrap vcpkg: ${VCPKG_BOOTSTRAP} failed (${bootstrap_result})"
      )
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

# vcpkg's toolchain runs `vcpkg install` inside project() on every configure,
# and a run that installs nothing still costs seconds. It runs only when
# something it reads has changed since the install that last ran: the
# manifest, the registry configuration, the triplets and what they include,
# the feature list, the triplet, and the vcpkg tool. The root writes the
# stamp once project() has returned, which is after the install succeeded.
# AL_VCPKG_INSTALL off leaves the ports to you; deleting the stamp file
# forces one install.
option(
  AL_VCPKG_INSTALL
  "Have vcpkg install the manifest's ports at configure when their inputs have changed"
  ON
)
set(AL_VCPKG_INSTALL_STAMP_FILE "${CMAKE_BINARY_DIR}/vcpkg-install.stamp")
if(AL_VCPKG_INSTALL)
  file(GLOB al_vcpkg_triplet_files "${CMAKE_SOURCE_DIR}/cmake/triplets/*.cmake")
  set(al_vcpkg_install_key "")
  foreach(
    input
    IN
    ITEMS
      "${CMAKE_SOURCE_DIR}/vcpkg.json"
      "${CMAKE_SOURCE_DIR}/vcpkg-configuration.json"
      "${CMAKE_CURRENT_LIST_DIR}/AlchemyTarget.cmake"
      ${al_vcpkg_triplet_files}
  )
    file(SHA256 "${input}" input_hash)
    string(APPEND al_vcpkg_install_key "${input}=${input_hash};")
  endforeach()
  if(DEFINED VCPKG_EXECUTABLE AND EXISTS "${VCPKG_EXECUTABLE}")
    file(SIZE "${VCPKG_EXECUTABLE}" vcpkg_size)
    file(TIMESTAMP "${VCPKG_EXECUTABLE}" vcpkg_stamp)
    string(APPEND al_vcpkg_install_key "vcpkg=${vcpkg_size}/${vcpkg_stamp};")
  endif()
  string(APPEND al_vcpkg_install_key "triplet=${VCPKG_TARGET_TRIPLET};")
  string(APPEND al_vcpkg_install_key "features=${VCPKG_MANIFEST_FEATURES};")
  string(APPEND al_vcpkg_install_key "toolchain=${CMAKE_TOOLCHAIN_FILE};")

  if(DEFINED VCPKG_INSTALLED_DIR)
    set(al_vcpkg_installed_dir "${VCPKG_INSTALLED_DIR}")
  else()
    set(al_vcpkg_installed_dir "${CMAKE_BINARY_DIR}/vcpkg_installed")
  endif()
  set(al_vcpkg_last_key "")
  if(EXISTS "${AL_VCPKG_INSTALL_STAMP_FILE}")
    file(READ "${AL_VCPKG_INSTALL_STAMP_FILE}" al_vcpkg_last_key)
  endif()
  if(
    al_vcpkg_last_key STREQUAL al_vcpkg_install_key
    AND EXISTS "${al_vcpkg_installed_dir}/vcpkg/status"
  )
    set(
      VCPKG_MANIFEST_INSTALL
      OFF
      CACHE BOOL
      "Decided by AL_VCPKG_INSTALL and the install stamp"
      FORCE
    )
    message(STATUS "vcpkg: nothing changed since the last install, not running it")
  else()
    set(
      VCPKG_MANIFEST_INSTALL
      ON
      CACHE BOOL
      "Decided by AL_VCPKG_INSTALL and the install stamp"
      FORCE
    )
    set(AL_VCPKG_INSTALL_KEY "${al_vcpkg_install_key}")
  endif()
else()
  set(
    VCPKG_MANIFEST_INSTALL
    OFF
    CACHE BOOL
    "Decided by AL_VCPKG_INSTALL and the install stamp"
    FORCE
  )
endif()
