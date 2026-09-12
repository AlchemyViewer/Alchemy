# -*- cmake -*-
#
# The configuration report printed at the end of the root CMakeLists.txt,
# and the FeatureSummary registration behind `--log-level=VERBOSE`.
#
# Every row reads the variable the build itself reads -- the option(),
# CMAKE_CXX_COMPILER_ID, VCPKG_TARGET_TRIPLET, the directory TESTS property --
# never a copy kept for display, so the report cannot disagree with the
# build. Every row prints every time, including the ones that say "off",
# so a CI log can be grepped for any of them.

include_guard()
include(FeatureSummary)

# The option() and cmake_dependent_option() switches the root defines, in the
# order the root defines them. Descriptions come from the cache HELPSTRING so
# they are written once.
set(AL_CONFIGURATION_OPTIONS
    BUILD_VIEWER
    BUILD_APPEARANCE_UTIL
    BUILD_TESTING
    BUILD_HEADLESS
    BUILD_DULLAHAN_EXAMPLE
    BUILD_EXAMPLE_PLUGIN
    BUILD_CEF_PLUGIN
    BUILD_VLC_PLUGIN
    BUILD_GSTREAMER_PLUGIN
    USE_LINUX_VOLUME_CATCHER
    USE_NDOF
    USE_LTO
    USE_OPENXR
    USE_PRECOMPILED_HEADERS
    USE_FMODSTUDIO
    USE_FAUDIO
    USE_OPENAL
    AL_WARNINGS_AS_ERRORS
    INSTALL_PROPRIETARY
    USE_KDU
    USE_DISCORD
    USE_SDL_WINDOW
    USE_NSSPELLCHECKER
    USE_WINSPELLCHECK
    RELEASE_CRASH_REPORTING
    NON_RELEASE_CRASH_REPORTING
    USE_BUGSPLAT
    USE_SENTRY
    USE_TRACY
    USE_TRACY_ON_DEMAND
    USE_TRACY_LOCAL_ONLY
    USE_TRACY_GPU
    USE_TRACY_GUI
    ENABLE_SIGNING
    USE_VELOPACK
    DISABLE_RELEASE_DEBUG_LOGGING
    PACKAGE
    )

function(al_feature_summary)
  foreach(opt IN LISTS AL_CONFIGURATION_OPTIONS)
    if(NOT DEFINED ${opt})
      continue()
    endif()
    get_property(doc CACHE ${opt} PROPERTY HELPSTRING)
    if(NOT doc OR doc MATCHES "NOTFOUND$")
      set(doc "forced by a dependent option")
    endif()
    add_feature_info(${opt} ${opt} "${doc}")
  endforeach()

  feature_summary(WHAT ENABLED_FEATURES DISABLED_FEATURES VAR summary)
  message(VERBOSE "${summary}")
  feature_summary(WHAT PACKAGES_NOT_FOUND QUIET_ON_EMPTY
                  DESCRIPTION "Packages not found:")
endfunction()

function(_al_report_row label value)
  string(LENGTH "${label}" len)
  math(EXPR pad "15 - ${len}")
  if(pad LESS 1)
    set(pad 1)
  endif()
  string(REPEAT " " ${pad} spaces)
  message(STATUS "  ${label}${spaces}${value}")
endfunction()

# Joins a list for display, or the given word when the list is empty.
function(_al_report_list out empty_word)
  if(ARGN)
    list(JOIN ARGN ", " joined)
    set(${out} "${joined}" PARENT_SCOPE)
  else()
    set(${out} "${empty_word}" PARENT_SCOPE)
  endif()
endfunction()

# Number of add_test() registrations under a directory, recursively. The
# TESTS directory property is per directory, so this walks SUBDIRECTORIES.
function(_al_count_tests dir out)
  get_property(tests DIRECTORY "${dir}" PROPERTY TESTS)
  list(LENGTH tests count)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(sub IN LISTS subdirs)
    _al_count_tests("${sub}" sub_count)
    math(EXPR count "${count} + ${sub_count}")
  endforeach()
  set(${out} ${count} PARENT_SCOPE)
endfunction()

# Mirrors the ISA selection in 00-Common.cmake: Darwin ignores AL_ISA_TIER
# and pins x86_64 to SSE4.2; Windows and Linux build the named tier.
function(_al_isa_description out)
  if(BUILD_TARGET_IS_ARM64)
    set(isa "arm64")
  elseif(DARWIN)
    set(isa "SSE4.2 (x86-64-v2, fixed for macOS x86_64)")
  elseif(AL_ISA_TIER STREQUAL "v4")
    set(isa "AVX-512 (x86-64-v4)")
  elseif(AL_ISA_TIER STREQUAL "v3")
    set(isa "AVX2 (x86-64-v3)")
  elseif(AL_ISA_TIER STREQUAL "v2")
    set(isa "SSE4.2 (x86-64-v2)")
  else()
    set(isa "baseline (x86-64)")
  endif()
  set(${out} "${isa}" PARENT_SCOPE)
endfunction()

function(al_configuration_report)
  message(STATUS "")
  message(STATUS "${VIEWER_CHANNEL} ${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION} (revision from ${VIEWER_VERSION_REVISION_SOURCE})")

  # Toolchain
  if(LL_GENERATOR_IS_MULTI_CONFIG)
    set(generator "${CMAKE_GENERATOR}, multi-config")
    set(configs "${CMAKE_CONFIGURATION_TYPES}")
    if(CMAKE_DEFAULT_BUILD_TYPE)
      string(APPEND configs " (default ${CMAKE_DEFAULT_BUILD_TYPE})")
    endif()
  else()
    set(generator "${CMAKE_GENERATOR}, single-config")
    set(configs "${CMAKE_BUILD_TYPE}")
  endif()
  if(CMAKE_GENERATOR_PLATFORM)
    string(APPEND generator ", platform ${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    string(APPEND generator ", toolset ${CMAKE_GENERATOR_TOOLSET}")
  endif()
  _al_report_row("Generator" "${generator}")
  _al_report_row("Configs" "${configs}")

  set(compiler "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT
     AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL CMAKE_CXX_COMPILER_ID)
    string(APPEND compiler " (${CMAKE_CXX_COMPILER_FRONTEND_VARIANT} frontend)")
  endif()
  string(APPEND compiler ", ${ARCH}")
  _al_report_row("Compiler" "${compiler}")

  set(linker "${CMAKE_CXX_COMPILER_LINKER_ID} ${CMAKE_CXX_COMPILER_LINKER_VERSION}")
  if(CMAKE_LINKER_TYPE)
    string(APPEND linker " (CMAKE_LINKER_TYPE ${CMAKE_LINKER_TYPE})")
  endif()
  _al_report_row("Linker" "${linker}")
  _al_report_row("C++ standard" "${CMAKE_CXX_STANDARD}")

  _al_report_list(features "none" ${VCPKG_MANIFEST_FEATURES})
  _al_report_row("vcpkg" "${VCPKG_TARGET_TRIPLET}, features: ${features}")

  # Code generation
  _al_isa_description(isa)
  _al_report_row("ISA" "${isa}")
  _al_report_row("LTO" "${USE_LTO}")
  _al_report_row("PCH" "${USE_PRECOMPILED_HEADERS}")

  _al_report_list(sanitizers_text "none" ${AL_SANITIZERS})
  _al_report_row("Sanitizers" "${sanitizers_text}")

  if(CMAKE_COMPILE_WARNING_AS_ERROR)
    _al_report_row("Warnings" "as errors")
  else()
    _al_report_row("Warnings" "not fatal")
  endif()

  # Platform backends
  if(USE_SDL_WINDOW)
    _al_report_row("Window" "SDL3")
  else()
    _al_report_row("Window" "native")
  endif()

  set(audio)
  if(USE_FMODSTUDIO)
    list(APPEND audio "FMOD Studio")
  endif()
  if(USE_FAUDIO)
    list(APPEND audio "FAudio")
  endif()
  if(USE_OPENAL)
    list(APPEND audio "OpenAL")
  endif()
  _al_report_list(audio_text "none" ${audio})
  _al_report_row("Audio" "${audio_text}")

  if(USE_NSSPELLCHECKER)
    _al_report_row("Spellcheck" "NSSpellChecker")
  elseif(USE_WINSPELLCHECK)
    _al_report_row("Spellcheck" "Windows Spell Checking API")
  else()
    _al_report_row("Spellcheck" "Hunspell")
  endif()

  if(USE_KDU)
    _al_report_row("JPEG2000" "Kakadu")
  else()
    _al_report_row("JPEG2000" "OpenJPEG")
  endif()

  if(USE_TRACY)
    set(tracy)
    if(USE_TRACY_ON_DEMAND)
      list(APPEND tracy "on-demand")
    endif()
    if(USE_TRACY_LOCAL_ONLY)
      list(APPEND tracy "local only")
    endif()
    if(USE_TRACY_GPU)
      list(APPEND tracy "GPU")
    endif()
    if(USE_TRACY_GUI)
      list(APPEND tracy "GUI")
    endif()
    _al_report_list(tracy_text "default" ${tracy})
    _al_report_row("Profiler" "Tracy (${tracy_text})")
  else()
    _al_report_row("Profiler" "off")
  endif()

  set(crash)
  if(USE_BUGSPLAT)
    list(APPEND crash "BugSplat '${BUGSPLAT_DB}'")
  endif()
  if(USE_SENTRY)
    list(APPEND crash "Sentry")
  endif()
  if(crash)
    set(sending)
    if(RELEASE_CRASH_REPORTING)
      list(APPEND sending "release")
    endif()
    if(NON_RELEASE_CRASH_REPORTING)
      list(APPEND sending "developer")
    endif()
    _al_report_list(crash_text "off" ${crash})
    _al_report_list(sending_text "no" ${sending})
    _al_report_row("Crash reports" "${crash_text}; sent from ${sending_text} builds")
  else()
    _al_report_row("Crash reports" "off")
  endif()

  # Components
  set(targets)
  if(BUILD_VIEWER)
    list(APPEND targets "viewer")
  endif()
  if(BUILD_APPEARANCE_UTIL)
    list(APPEND targets "appearance utility")
  endif()
  if(BUILD_DULLAHAN_EXAMPLE)
    list(APPEND targets "dullahan example")
  endif()
  _al_report_list(targets_text "none" ${targets})
  _al_report_row("Targets" "${targets_text}")

  set(plugins)
  if(BUILD_CEF_PLUGIN)
    list(APPEND plugins "CEF")
  endif()
  if(BUILD_VLC_PLUGIN)
    list(APPEND plugins "VLC")
  endif()
  if(BUILD_GSTREAMER_PLUGIN)
    list(APPEND plugins "GStreamer")
  endif()
  if(USE_LINUX_VOLUME_CATCHER)
    list(APPEND plugins "CEF volume catcher")
  endif()
  if(BUILD_EXAMPLE_PLUGIN)
    list(APPEND plugins "example")
  endif()
  _al_report_list(plugins_text "none" ${plugins})
  _al_report_row("Media plugins" "${plugins_text}")

  set(extras)
  if(USE_NDOF)
    list(APPEND extras "NDOF")
  endif()
  if(USE_OPENXR)
    list(APPEND extras "OpenXR")
  endif()
  if(USE_DISCORD)
    list(APPEND extras "Discord")
  endif()
  _al_report_list(extras_text "none" ${extras})
  _al_report_row("Extras" "${extras_text}")

  _al_report_row("Headless" "${BUILD_HEADLESS}")
  _al_report_row("Proprietary" "${INSTALL_PROPRIETARY}")
  _al_report_row("Grid" "${GRID}")

  if(DISABLE_RELEASE_DEBUG_LOGGING)
    _al_report_row("Debug logging" "disabled in Release")
  else()
    _al_report_row("Debug logging" "enabled in Release")
  endif()

  # Packaging
  if(DEFINED PACKAGE AND NOT PACKAGE)
    _al_report_row("Installer" "none (PACKAGE off)")
  elseif(LINUX)
    _al_report_row("Installer" "tar.xz")
  elseif(USE_VELOPACK)
    _al_report_row("Installer" "Velopack")
  else()
    _al_report_row("Installer" "none (USE_VELOPACK off)")
  endif()
  if(ENABLE_SIGNING)
    _al_report_row("Signing" "identity '${SIGNING_IDENTITY}'")
  else()
    _al_report_row("Signing" "off")
  endif()

  if(BUILD_TESTING)
    _al_count_tests("${INDRA_SOURCE_DIR}" test_count)
    _al_report_row("Tests" "on, ${test_count} registered")
  else()
    _al_report_row("Tests" "off")
  endif()

  if(Python3_EXECUTABLE)
    _al_report_row("Python" "${Python3_VERSION} (${Python3_EXECUTABLE})")
  else()
    _al_report_row("Python" "not found")
  endif()

  if(LL_GENERATOR_IS_MULTI_CONFIG)
    _al_report_row("Staging" "${INDRA_BINARY_DIR}/newview/<config>")
  elseif(DARWIN)
    _al_report_row("Staging" "${INDRA_BINARY_DIR}/newview")
  else()
    _al_report_row("Staging" "${INDRA_BINARY_DIR}/newview/packaged")
  endif()
  message(STATUS "")
endfunction()
