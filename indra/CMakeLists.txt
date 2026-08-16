# -*- cmake -*-

# cmake_minimum_required should appear before any
# other commands to guarantee full compatibility
# with the version specified
# 3.27 is our minimum required cmake version while 4.2 is our cmake policy max version(meaning any policies of version 4.2 or below are set to NEW)
cmake_minimum_required(VERSION 3.27...4.2 FATAL_ERROR)

include(CMakeDependentOption)

set(ROOT_PROJECT_NAME "Alchemy" CACHE STRING
    "The root project/makefile/solution name. Defaults to Alchemy.")

# Set our base channel
set(VIEWER_CHANNEL "Alchemy Test" CACHE STRING "Viewer Channel Name")

# Default deploy grid
set(GRID agni CACHE STRING "Target Grid")

# USE_VELOPACK controls whether to use Velopack for installer packaging (instead of NSIS/DMG)
option(USE_VELOPACK "Use Velopack for installer packaging" OFF)

# Build executable options
option(BUILD_VIEWER "Build the viewer project" ON)
option(BUILD_APPEARANCE_UTIL "Build the appearance utility project" OFF)
option(BUILD_TESTING "Build and run unit and integration tests: disable for build timing runs to reduce variation" OFF)
option(BUILD_BENCHMARKS "Build Google Benchmark targets" OFF)
option(BUILD_HEADLESS "Build with support for headless mesa OSR rendering backend" OFF)

# Dullahan build options
option(BUILD_DULLAHAN_EXAMPLE "Build the Dullahan OpenGL example application" OFF)

# Plugin Build Options
option(BUILD_EXAMPLE_PLUGIN "Build support for SLPlugin example plugin" ON)
option(BUILD_CEF_PLUGIN "Build support for SLPlugin CEF web plugin." ON)
option(BUILD_VLC_PLUGIN "Build support for SLPlugin VLC media plugin." ON)
cmake_dependent_option(BUILD_GSTREAMER_PLUGIN "Build support for SLPlugin Gstreamer media plugin." ON "UNIX;NOT APPLE" OFF)

# Build feature and optimization options
option(OS_DRAG_DROP "Build the viewer with OS level drag and drop support" ON)
option(USE_NDOF "Use NDOF space navigator joystick library." ON)
option(USE_LTO "Enable Link Time Optimization" OFF)
option(USE_NVAPI "Use NVidia NVAPI library for gpu profile support (Windows Only)" ON)
option(USE_OPENXR "Enable building with OpenXR support (Experimental)" OFF)
option(USE_PRECOMPILED_HEADERS "Enable use of precompiled header directives where supported." ON)

# Audio Engines
option(USE_FMODSTUDIO "Enable support for the FMOD Studio audio engine" OFF)
option(USE_FAUDIO "Enable support for the FAudio audio engine" ON)
option(USE_OPENAL "Enable support for the OpenAL audio engine" OFF)

# SIMD Options (x86_64 only)
option(USE_SSE4_2 "Use SSE4.2/x86-64-v2 instruction sets (x86_64 only)" OFF)
option(USE_AVX "Use AVX/x86-64-v2+avx instruction sets (x86_64 only)" OFF)
option(USE_AVX2 "Use AVX2/x86_64-v3 instruction sets (x86_64 only)" OFF)

# Sanitizers (Mac and Linux Only)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_THREADSAN "Enable Thread Sanitizer" OFF)

# Proprietary Libraries
option(INSTALL_PROPRIETARY "Build with support for non-free libraries" OFF)
cmake_dependent_option(USE_KDU "Enable support for the Kakadu JPEG2000 decoding library" ON "INSTALL_PROPRIETARY" OFF)

# TODO: Replace with commented option below once Havok support is added to vcpkg
option(HAVOK "Use Havok physics library" OFF)
#cmake_dependent_option(HAVOK "Use Havok physics library" ON "INSTALL_PROPRIETARY" OFF)

# Uncomment when support for Discord is finalized
#cmake_dependent_option(USE_DISCORD "Enable Discord SDK and Rich Prescence support" ON "INSTALL_PROPRIETARY" OFF)

# SDL Options
if (UNIX)
  option(USE_SDL_WINDOW "Build with SDL based window management and input" ON)
else ()
  option(USE_SDL_WINDOW "Build with SDL based window management and input (experimental on Windows)" OFF)
endif ()

# Spell checking backend
cmake_dependent_option(USE_NSSPELLCHECKER "Use the native macOS NSSpellChecker instead of Hunspell" ON "APPLE" OFF)
cmake_dependent_option(USE_WINSPELLCHECK "Use the native Windows Spell Checking API instead of Hunspell" ON "WIN32" OFF)

# Configure crash reporting
option(RELEASE_CRASH_REPORTING "Enable use of crash reporting in release builds" OFF)
option(NON_RELEASE_CRASH_REPORTING "Enable use of crash reporting in developer builds" OFF)

# Bugsplat setup
if (INSTALL_PROPRIETARY AND (WIN32 OR APPLE))
    # Note that viewer_manifest.py makes decision based on BUGSPLAT_DB and not USE_BUGSPLAT
    if (BUGSPLAT_DB)
        set(USE_BUGSPLAT ON  CACHE BOOL "Use the BugSplat crash reporting system")
    else (BUGSPLAT_DB)
        set(USE_BUGSPLAT OFF CACHE BOOL "Use the BugSplat crash reporting system")
    endif (BUGSPLAT_DB)
else ()
    set(USE_BUGSPLAT OFF CACHE BOOL "Use the BugSplat crash reporting system")
    set(BUGSPLAT_DB "" CACHE STRING "BugSplat crash database name")
endif ()

# Sentry Setup
option(USE_SENTRY "Use the Sentry crash reporting system" OFF)

# Tracy Profiler
# Default Tracy profiling on for test builds, but off for all others
set(BUILDING_TEST_CHANNEL OFF)
string(TOLOWER "${VIEWER_CHANNEL}" channel_lower)
if(channel_lower MATCHES "^alchemy test")
  set(BUILDING_TEST_CHANNEL ON)
endif()
cmake_dependent_option(USE_TRACY "Enable support for Tracy profiler" ON "BUILDING_TEST_CHANNEL" OFF)
option(USE_TRACY_ON_DEMAND "Use on-demand Tracy profiling." ON)
option(USE_TRACY_LOCAL_ONLY "Disallow remote Tracy profiling." ON)
option(USE_TRACY_GPU "Use Tracy GPU profiling" OFF)
option(USE_TRACY_GUI "Enable building Tracy profiler GUI" OFF)

# Signing
option(ENABLE_SIGNING "Enable signing the viewer" OFF)
set(SIGNING_IDENTITY "" CACHE STRING "Specifies the signing identity to use, if necessary.")

# macOS deploy target(must be set BEFORE first project call)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version to target for deployment (at runtime); newer APIs weak linked. Set to empty string for default value.")

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

if (BUILD_TESTING)
  list(APPEND VCPKG_MANIFEST_FEATURES "google-test")
endif()

if (BUILD_BENCHMARKS)
  list(APPEND VCPKG_MANIFEST_FEATURES "google-benchmark")
endif()

if (USE_KDU)
  list(APPEND VCPKG_MANIFEST_FEATURES "kdu")
endif()

# Setup Tracy feature flags
if (USE_TRACY)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy")
endif()

if (USE_TRACY_LOCAL_ONLY)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy-local")
endif()

if(USE_TRACY_ON_DEMAND)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy-on-demand")
endif()

if(USE_TRACY_GUI)
  list(APPEND VCPKG_MANIFEST_FEATURES "tracy-gui")
endif()

if(USE_OPENXR)
  list(APPEND VCPKG_MANIFEST_FEATURES "openxr")
endif()

if(USE_BUGSPLAT)
  list(APPEND VCPKG_MANIFEST_FEATURES "bugsplat")
endif()

if(USE_VELOPACK)
  list(APPEND VCPKG_MANIFEST_FEATURES "velopack")
endif()

if(USE_SENTRY)
  list(APPEND VCPKG_MANIFEST_FEATURES "sentry")
endif()

if(BUILD_HEADLESS)
  list(APPEND VCPKG_MANIFEST_FEATURES "offscreen")
endif()

if(USE_FAUDIO)
  list(APPEND VCPKG_MANIFEST_FEATURES "faudio")
endif()

if(USE_OPENAL)
  list(APPEND VCPKG_MANIFEST_FEATURES "openal")
endif()

if(BUILD_DULLAHAN_EXAMPLE)
  list(APPEND VCPKG_MANIFEST_FEATURES "dullahan-example")
endif()

if (NOT USE_NSSPELLCHECKER AND NOT USE_WINSPELLCHECK)
  list(APPEND VCPKG_MANIFEST_FEATURES "hunspell")
endif()

# Export compile_commands.json for ninja and makefile generators
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT USE_PRECOMPILED_HEADERS)
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
endif()

# Include our cmake module directory
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_BINARY_DIR}")

# Setup VCPKG toolchains and targets
include(BootstrapVcpkg)

# Generate base project after setting pre-generation variables
project(${ROOT_PROJECT_NAME})

# Location of indra directory
set(INDRA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(INDRA_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# Set up common variables
include(Variables)

# Enable supported languages
enable_language(C CXX)

if(DARWIN)
  enable_language(OBJC OBJCXX)
endif()

# Set up Python
include(Python)

# Set up our base build system variable and compiler flags
include(00-Common)
include(Linking)

# Set up version number generation
include(BuildVersion)

if (BUILD_TESTING)
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

# Declare All Dependency Includes
include(APR)
include(AVIF)
include(Boost)
include(bugsplat)
include(CEFPlugin)
include(Collada-Dom)
include(CURL)
include(DBUS)
include(Discord)
include(EXPAT)
include(FastFloat)
include(FAudio)
include(FMODSTUDIO)
include(FMT)
include(FontConfig)
include(FreeType)
include(GLEXT)
include(GLIB)
include(GLM)
include(Harfbuzz)
include(Havok)
include(Hunspell)
include(JPEG)
include(LLKDU)
include(LLPhysicsExtensions)
include(Mesa)
include(Meshoptimizer)
include(Mikktspace)
include(NDOF)
include(NSSpellChecker)
include(NVAPI)
include(OpenAL)
include(OpenGL)
include(OpenJPEG)
include(OpenSSL)
include(OpenXR)
include(PlutoSVG)
include(PNG)
include(SDL3)
include(Sentry)
include(simdjson)
include(simdutf)
include(SSE2NEON)
include(TinyEXR)
include(TinyGLTF)
include(Tracy)
include(Tut)
include(VHACD)
include(Vorbis)
include(WebP)
include(WebRTC)
include(websocketpp)
include(WinSpellCheck)
include(xxHash)
include(ZLIBNG)

# Add our test macros
if (BUILD_TESTING)
  include(LLAddBuildTest)
  include(GTest)
endif ()

if (BUILD_BENCHMARKS)
  include(Benchmark)
endif ()

# Third party system library copy step
include(Copy3rdPartyLibs)

# Add our cmake modules to cmake
add_subdirectory(cmake)

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
add_subdirectory(llwebrtc)
add_subdirectory(llwindow)
add_subdirectory(llxml)
add_subdirectory(llplugin)
add_subdirectory(llui)
add_subdirectory(viewer_components)

if(BUILD_TESTING)
  # Legacy C++ tests.
  add_subdirectory(test)
endif()

# viewer media plugins
add_subdirectory(media_plugins)

if (BUILD_APPEARANCE_UTIL)
  # Avatar Appearance Utility
  add_subdirectory(llappearanceutility)
endif()

# Viewer
add_subdirectory(newview)

add_subdirectory(doxygen EXCLUDE_FROM_ALL)

# sets the 'startup project' for debugging from visual studio.
set_property(
        DIRECTORY
        PROPERTY VS_STARTUP_PROJECT ${VIEWER_BINARY_NAME}
        )

if (BUILD_TESTING)
  # Define after the custom targets are created so
  # individual apps can add themselves as dependencies
  add_subdirectory(integration_tests)
endif ()
