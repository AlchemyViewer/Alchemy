# -*- cmake -*-

# The floor is 4.0: CMAKE_MSVC_RUNTIME_CHECKS, which the Debug configuration
# relies on, is a plain unknown variable on anything older. Policies through
# 4.4 are NEW.
cmake_minimum_required(VERSION 4.0...4.4 FATAL_ERROR)

include(CMakeDependentOption)

set(AL_PROJECT_NAME "Alchemy" CACHE STRING
    "The root project/makefile/solution name. Defaults to Alchemy.")

# Set our base channel
set(AL_CHANNEL "Alchemy Test" CACHE STRING "Viewer Channel Name")

# Default deploy grid
set(AL_GRID agni CACHE STRING "Target Grid")

# AL_USE_VELOPACK controls whether to use Velopack for installer packaging (instead of NSIS/DMG)
option(AL_USE_VELOPACK "Use Velopack for installer packaging" OFF)

# Build executable options
option(AL_BUILD_VIEWER "Build the viewer project" ON)
option(AL_BUILD_APPEARANCE_UTILITY "Build the appearance utility project" OFF)
option(AL_BUILD_TESTS "Build and run unit and integration tests: disable for build timing runs to reduce variation" OFF)
option(AL_BUILD_DOCS "Add the doc target that generates the API documentation with Doxygen" OFF)
option(AL_BUILD_HEADLESS "Build with support for headless mesa OSR rendering backend" OFF)
option(AL_BUILD_SHARED_LLCOMMON "Build llcommon as a shared library" OFF)
option(AL_BUILD_PACKAGE "Add a package target that builds an installer package" ON)

# CMake's own BUILD_TESTING, from the CTest module, is what ctest and IDE
# integrations read; it follows AL_BUILD_TESTS.
set(BUILD_TESTING ${AL_BUILD_TESTS})

# Dullahan build options
option(AL_BUILD_DULLAHAN_EXAMPLE "Build the Dullahan OpenGL example application" OFF)

# Plugin Build Options
option(AL_BUILD_EXAMPLE_PLUGIN "Build support for SLPlugin example plugin" ON)
option(AL_BUILD_CEF_PLUGIN "Build support for SLPlugin CEF web plugin." ON)
option(AL_BUILD_VLC_PLUGIN "Build support for SLPlugin VLC media plugin." ON)
cmake_dependent_option(AL_BUILD_GSTREAMER_PLUGIN "Build support for SLPlugin Gstreamer media plugin." ON "UNIX;NOT APPLE" OFF)
# Off since CEF 139: dlopen of the PulseAudio/PipeWire client libraries fails inside the CEF host process.
cmake_dependent_option(AL_USE_LINUX_VOLUME_CATCHER "Build the PulseAudio/PipeWire volume catcher into the CEF media plugin." OFF "UNIX;NOT APPLE;AL_BUILD_CEF_PLUGIN" OFF)

# Build feature and optimization options
option(AL_USE_NDOF "Use NDOF space navigator joystick library." ON)
option(AL_USE_LTO "Enable Link Time Optimization" OFF)
option(AL_USE_OPENXR "Enable building with OpenXR support (Experimental)" OFF)
option(AL_USE_PRECOMPILED_HEADERS "Enable use of precompiled header directives where supported." ON)

# Audio Engines
option(AL_USE_FMODSTUDIO "Enable support for the FMOD Studio audio engine" OFF)
option(AL_USE_FAUDIO "Enable support for the FAudio audio engine" ON)
option(AL_USE_OPENAL "Enable support for the OpenAL audio engine" OFF)

# Instruction set: an x86-64 microarchitecture level, for the viewer and the
# vcpkg triplet alike (baseline = x86-64, v2 = SSE4.2, v3 = AVX2, v4 = AVX-512).
# Ignored on macOS, where x86_64 is pinned to SSE4.2 for Rosetta 2.
set(AL_ISA_TIER "v3" CACHE STRING "x86-64 microarchitecture level to build for: baseline, v2, v3 or v4")
set_property(CACHE AL_ISA_TIER PROPERTY STRINGS baseline v2 v3 v4)
if(NOT AL_ISA_TIER MATCHES "^(baseline|v2|v3|v4)$")
  message(FATAL_ERROR "AL_ISA_TIER must be one of baseline, v2, v3, v4 (got '${AL_ISA_TIER}')")
endif()

# Sanitizers (GCC and Clang only): any of address, undefined, thread.
set(AL_SANITIZERS "" CACHE STRING "Sanitizers to build with: a list drawn from address, undefined, thread")

# Warnings
option(AL_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Proprietary Libraries
option(AL_ENABLE_PROPRIETARY "Build with support for non-free libraries" OFF)
cmake_dependent_option(AL_USE_KDU "Enable support for the Kakadu JPEG2000 decoding library" ON "AL_ENABLE_PROPRIETARY" OFF)

# The Discord Social SDK is downloaded per application from Discord and
# points to by AL_DISCORD_SDK_DIR; see cmake/Discord.cmake.
cmake_dependent_option(AL_USE_DISCORD "Enable Discord presence and friends through the Discord Social SDK" OFF "AL_ENABLE_PROPRIETARY" OFF)

# SDL Options
if (UNIX)
  option(AL_USE_SDL_WINDOW "Build with SDL based window management and input" ON)
else ()
  option(AL_USE_SDL_WINDOW "Build with SDL based window management and input (experimental on Windows)" OFF)
endif ()

# Spell checking backend
cmake_dependent_option(AL_USE_NSSPELLCHECKER "Use the native macOS NSSpellChecker instead of Hunspell" ON "APPLE" OFF)
cmake_dependent_option(AL_USE_WINSPELLCHECK "Use the native Windows Spell Checking API instead of Hunspell" ON "WIN32" OFF)

# Crash reporting
option(AL_ENABLE_CRASH_REPORTING "Send crash reports from this build" OFF)

# Bugsplat setup
if (AL_ENABLE_PROPRIETARY AND (WIN32 OR APPLE))
    if (AL_BUGSPLAT_DATABASE)
        set(AL_USE_BUGSPLAT ON  CACHE BOOL "Use the BugSplat crash reporting system")
    else (AL_BUGSPLAT_DATABASE)
        set(AL_USE_BUGSPLAT OFF CACHE BOOL "Use the BugSplat crash reporting system")
    endif (AL_BUGSPLAT_DATABASE)
else ()
    set(AL_USE_BUGSPLAT OFF CACHE BOOL "Use the BugSplat crash reporting system")
    set(AL_BUGSPLAT_DATABASE "" CACHE STRING "BugSplat crash database name")
endif ()

# Sentry Setup
option(AL_USE_SENTRY "Use the Sentry crash reporting system" OFF)

# Tracy Profiler
# Default Tracy profiling on for test builds, but off for all others
set(BUILDING_TEST_CHANNEL OFF)
string(TOLOWER "${AL_CHANNEL}" channel_lower)
if(channel_lower MATCHES "^alchemy test")
  set(BUILDING_TEST_CHANNEL ON)
endif()
cmake_dependent_option(AL_USE_TRACY "Enable support for Tracy profiler" ON "BUILDING_TEST_CHANNEL" OFF)
option(AL_ENABLE_TRACY_ON_DEMAND "Use on-demand Tracy profiling." ON)
option(AL_ENABLE_TRACY_LOCAL_ONLY "Disallow remote Tracy profiling." ON)
option(AL_ENABLE_TRACY_GPU "Use Tracy GPU profiling" OFF)
option(AL_BUILD_TRACY_GUI "Enable building Tracy profiler GUI" OFF)

# Debug logging stays available in Release builds of the Test channel only.
cmake_dependent_option(AL_ENABLE_RELEASE_DEBUG_LOGGING "Keep debug logging in Release builds" ON "BUILDING_TEST_CHANNEL" OFF)

# WebRTC voice. Off in sanitized builds, which cannot link libwebrtc.
option(AL_USE_WEBRTC "Build with WebRTC voice" ON)

# Signing
option(AL_ENABLE_SIGNING "Enable signing the viewer" OFF)
set(AL_SIGNING_IDENTITY "" CACHE STRING "Specifies the signing identity to use, if necessary.")

# Warn about any option name from before the AL_ scheme.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RetiredOptions.cmake)

# The oldest macOS the viewer runs on, shared with the triplets; must be set
# before the first project() call.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AlchemyTarget.cmake)
set(CMAKE_OSX_DEPLOYMENT_TARGET "${AL_MACOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS version to target for deployment (at runtime); newer APIs weak linked. Set to empty string for default value.")

if (APPLE)
  # Disable app-local dependencies on macOS, since we use a custom bundle structure and don't want vcpkg to copy dependencies into the bundle.
  set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Use app-local dependencies instead of system-installed dependencies")
endif()

# Set our supported configuration types
set(CMAKE_CONFIGURATION_TYPES "Debug;OptDebug;RelWithDebInfo;Release" CACHE STRING "Supported build types.")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Build type.  One of: Debug OptDebug Release RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

##########################################################################
# vcpkg feature flags MUST be set before the first project() call below  #
##########################################################################

if (AL_USE_KDU)
  list(APPEND VCPKG_MANIFEST_FEATURES "kdu")
endif()

# Setup Tracy feature flags
if (AL_USE_TRACY)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy")
endif()

if (AL_USE_TRACY AND AL_ENABLE_TRACY_LOCAL_ONLY)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy-local")
endif()

if(AL_USE_TRACY AND AL_ENABLE_TRACY_ON_DEMAND)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy-on-demand")
endif()

if(AL_USE_TRACY AND AL_BUILD_TRACY_GUI)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy-gui")
endif()

if(AL_USE_OPENXR)
  list(APPEND VCPKG_MANIFEST_FEATURES "openxr")
endif()

if(AL_USE_WEBRTC)
  list(APPEND VCPKG_MANIFEST_FEATURES "webrtc")
endif()

if(AL_USE_BUGSPLAT)
  list(APPEND VCPKG_MANIFEST_FEATURES "bugsplat")
endif()

if(AL_USE_VELOPACK)
  list(APPEND VCPKG_MANIFEST_FEATURES "velopack")
endif()

if(AL_USE_SENTRY)
  list(APPEND VCPKG_MANIFEST_FEATURES "sentry")
endif()

if(AL_BUILD_HEADLESS)
  list(APPEND VCPKG_MANIFEST_FEATURES "offscreen")
endif()

if(AL_USE_FAUDIO)
  list(APPEND VCPKG_MANIFEST_FEATURES "faudio")
endif()

if(AL_USE_OPENAL)
  list(APPEND VCPKG_MANIFEST_FEATURES "openal")
endif()

if(AL_BUILD_DULLAHAN_EXAMPLE)
  list(APPEND VCPKG_MANIFEST_FEATURES "dullahan-example")
endif()

if (NOT AL_USE_NSSPELLCHECKER AND NOT AL_USE_WINSPELLCHECK)
  list(APPEND VCPKG_MANIFEST_FEATURES "hunspell")
endif()

# Export compile_commands.json for ninja and makefile generators
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The install rules stage the viewer after every link; report only what
# they changed.
set(CMAKE_INSTALL_MESSAGE LAZY)

if(NOT AL_USE_PRECOMPILED_HEADERS)
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
endif()

# Include our cmake module directory
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Setup VCPKG toolchains and targets
include(BootstrapVcpkg)

# Generate base project after setting pre-generation variables
project(${AL_PROJECT_NAME} LANGUAGES C CXX)

# The vcpkg install ran inside project(); record what it was run against so
# the next configure can skip it. See BootstrapVcpkg.cmake.
if(DEFINED AL_VCPKG_INSTALL_KEY)
  file(WRITE "${AL_VCPKG_INSTALL_STAMP_FILE}" "${AL_VCPKG_INSTALL_KEY}")
endif()

# Location of indra directory
set(INDRA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(INDRA_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# Set up common variables
include(Variables)

if(DARWIN)
  enable_language(OBJC OBJCXX)
endif()

# Set up our base build system variable and compiler flags
include(00-Common)
include(Linking)

# Set up version number generation
include(BuildVersion)

if (AL_BUILD_TESTS)
  # Only the tests that spawn a Python peer need an interpreter.
  include(Python)

  set(CMAKE_SKIP_TEST_ALL_DEPENDENCY FALSE)
  include(ProcessorCount)
  ProcessorCount(NUM_CORES)

  # Set parallel level to decrease test runtime
  set(CMAKE_CTEST_ARGUMENTS -j${NUM_CORES} --output-on-failure)

  # Set up our test targets
  set(CTEST_TEST_TARGET_ALIAS BUILD_AND_RUN_TESTS)
  include(CTest)

  set_target_properties(BUILD_AND_RUN_TESTS
        PROPERTIES
        FOLDER "CMakePredefinedTargets"
        )

  add_custom_target(BUILD_TESTS)
  set_target_properties(BUILD_TESTS
        PROPERTIES
        FOLDER "CMakePredefinedTargets"
        )
endif ()

# Dependency modules, in dependency order. Each is included under a message
# context of its own name, so `cmake --log-context` attributes every line a
# module prints -- including the find_package() output inside it -- to that
# module.
set(AL_DEPENDENCY_MODULES
    Dependencies
    APR
    Boost
    bugsplat
    CEFPlugin
    CURL
    DBUS
    Discord
    FMODSTUDIO
    GLIB
    Harfbuzz
    Hunspell
    ICU
    LLKDU
    Mesa
    NDOF
    SDL3
    Sentry
    Tracy
    WebRTC
    )

foreach(module IN LISTS AL_DEPENDENCY_MODULES)
  list(APPEND CMAKE_MESSAGE_CONTEXT ${module})
  include(${module})
  list(POP_BACK CMAKE_MESSAGE_CONTEXT)
endforeach()

# Add our test macros
if (AL_BUILD_TESTS)
  include(LLAddBuildTest)
endif ()

# Add our cmake modules to cmake
add_subdirectory(cmake)
add_subdirectory(template_verifier)

add_subdirectory(dullahan)
add_subdirectory(llprecompiled)
add_subdirectory(llaudio)
add_subdirectory(llappearance)
add_subdirectory(llcharacter)
add_subdirectory(llcommon)
add_subdirectory(llcorehttp)
add_subdirectory(llimage)
add_subdirectory(llkdu)
add_subdirectory(llimagej2coj)
add_subdirectory(llinventory)
add_subdirectory(llmath)
add_subdirectory(llmessage)
add_subdirectory(llphysicsextensionsos)
add_subdirectory(llprimitive)
add_subdirectory(llrender)
add_subdirectory(llfilesystem)
if (AL_USE_WEBRTC)
  add_subdirectory(llwebrtc)
endif ()
add_subdirectory(llwindow)
add_subdirectory(llxml)
add_subdirectory(llplugin)
add_subdirectory(llui)
add_subdirectory(viewer_components)

if(AL_BUILD_TESTS)
  # Legacy C++ tests.
  add_subdirectory(test)
endif()

# viewer media plugins
add_subdirectory(media_plugins)

if (AL_BUILD_APPEARANCE_UTILITY)
  # Avatar Appearance Utility
  add_subdirectory(llappearanceutility)
endif()

# Viewer
add_subdirectory(newview)

if (AL_BUILD_DOCS)
  add_subdirectory(doxygen EXCLUDE_FROM_ALL)
endif ()

# sets the 'startup project' for debugging from visual studio.
set_property(
        DIRECTORY
        PROPERTY VS_STARTUP_PROJECT ${AL_VIEWER_BINARY_NAME}
        )

if (AL_BUILD_TESTS)
  # Define after the custom targets are created so
  # individual apps can add themselves as dependencies
  add_subdirectory(integration_tests)
endif ()

# Last, after every add_subdirectory(): every buildable target must carry the
# shared flags, and nothing below can change a report row.
al_check_flags_target()
include(ConfigurationReport)
al_feature_summary()
al_configuration_report()
