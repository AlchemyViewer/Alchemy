# -*- cmake -*-
#
# Definitions of variables used throughout the Second Life build
# process.
#
# Platform variables:
#
#   DARWIN  - Mac OS X
#   LINUX   - Linux
#   WINDOWS - Windows

include_guard()
# Relative and absolute paths to subtrees.

if(NOT DEFINED COMMON_CMAKE_DIR)
    set(COMMON_CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")
endif(NOT DEFINED COMMON_CMAKE_DIR)

set(LIBS_CLOSED_PREFIX)
set(LIBS_OPEN_PREFIX)
set(SCRIPTS_PREFIX ../scripts)
set(VIEWER_PREFIX)
set(INTEGRATION_TESTS_PREFIX)

option(LL_TESTS "Build and run unit and integration tests (disable for build timing runs to reduce variation" OFF)
if(DEFINED ENV{LL_TESTS})
  set(LL_TESTS $ENV{LL_TESTS} CACHE STRING "Build and run unit and integration tests (disable for build timing runs to reduce variation" FORCE)
else()
  set(LL_TESTS "" CACHE STRING "Build and run unit and integration tests (disable for build timing runs to reduce variation")
endif()

option(ENABLE_MEDIA_PLUGINS "Turn off building media plugins if they are imported by third-party library mechanism" ON)

# Compiler and toolchain options
option(INCREMENTAL_LINK "Use incremental linking on win32 builds (enable for faster links on some machines)" OFF)
option(USE_LTO "Enable global and interprocedural optimizations" OFF)
option(USE_ASAN "Enable address sanitizer for detection of memory issues" OFF)
option(USE_LEAKSAN "Enable address sanitizer for detection of memory leaks" OFF)
option(USE_UBSAN "Enable undefined behavior sanitizer" OFF)
option(USE_THDSAN "Enable thread sanitizer for detection of thread data races and mutexing issues" OFF)
if(USE_ASAN AND USE_LEAKSAN)
  message(FATAL_ERROR "You may only enable either USE_ASAN or USE_LEAKSAN not both")
elseif((USE_ASAN OR USE_LEAKSAN) AND USE_THDSAN)
  message(FATAL_ERROR "Address and Leak sanitizers are incompatible with thread sanitizer")
endif(USE_ASAN AND USE_LEAKSAN)
set(VIEWER_SYMBOL_FILE "" CACHE STRING "Name of tarball into which to place symbol files")

option(USE_CEF "Enable CEF media plugin" ON)
option(USE_VLC "Enable VLC media plugin" ON)

option(BUILD_EXPIREY "Use build expirey system" OFF)

if(LIBS_CLOSED_DIR)
  file(TO_CMAKE_PATH "${LIBS_CLOSED_DIR}" LIBS_CLOSED_DIR)
else(LIBS_CLOSED_DIR)
  set(LIBS_CLOSED_DIR ${CMAKE_SOURCE_DIR}/${LIBS_CLOSED_PREFIX})
endif(LIBS_CLOSED_DIR)
if(LIBS_COMMON_DIR)
  file(TO_CMAKE_PATH "${LIBS_COMMON_DIR}" LIBS_COMMON_DIR)
else(LIBS_COMMON_DIR)
  set(LIBS_COMMON_DIR ${CMAKE_SOURCE_DIR}/${LIBS_OPEN_PREFIX})
endif(LIBS_COMMON_DIR)
set(LIBS_OPEN_DIR ${LIBS_COMMON_DIR})

set(SCRIPTS_DIR ${CMAKE_SOURCE_DIR}/${SCRIPTS_PREFIX})
set(VIEWER_DIR ${CMAKE_SOURCE_DIR}/${VIEWER_PREFIX})

set(AUTOBUILD_INSTALL_DIR ${CMAKE_BINARY_DIR}/packages)

set(LIBS_PREBUILT_DIR ${AUTOBUILD_INSTALL_DIR} CACHE PATH
    "Location of prebuilt libraries.")

if (EXISTS ${CMAKE_SOURCE_DIR}/Server.cmake)
  # We use this as a marker that you can try to use the proprietary libraries.
  set(INSTALL_PROPRIETARY ON CACHE BOOL "Install proprietary binaries")
endif (EXISTS ${CMAKE_SOURCE_DIR}/Server.cmake)
set(TEMPLATE_VERIFIER_OPTIONS "" CACHE STRING "Options for scripts/template_verifier.py")
set(TEMPLATE_VERIFIER_MASTER_URL "https://github.com/AlchemyViewer/master-message-template/raw/master/message_template.msg" CACHE STRING "Location of the master message template")

# If someone has specified an address size, use that to determine the
# architecture.  Otherwise, let the architecture specify the address size.
if (ADDRESS_SIZE EQUAL 32)
  set(ARCH i686)
elseif (ADDRESS_SIZE EQUAL 64)
  set(ARCH x86_64)
else (ADDRESS_SIZE EQUAL 32)
    #message(STATUS "ADDRESS_SIZE is UNDEFINED")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(STATUS "Size of void pointer is detected as 8; ARCH is 64-bit")
      set(ARCH x86_64)
      set(ADDRESS_SIZE 64)
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
      message(STATUS "Size of void pointer is detected as 4; ARCH is 32-bit")
      set(ADDRESS_SIZE 32)
      set(ARCH i686)
    else()
      message(FATAL_ERROR "Unkown Architecture!")
    endif()
endif (ADDRESS_SIZE EQUAL 32)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  set(WINDOWS ON BOOL FORCE)
endif (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(LINUX ON BOOl FORCE)

  if (ADDRESS_SIZE EQUAL 32)
    set(DEB_ARCHITECTURE i386)
    set(FIND_LIBRARY_USE_LIB64_PATHS OFF)
    set(CMAKE_SYSTEM_LIBRARY_PATH /usr/lib32 ${CMAKE_SYSTEM_LIBRARY_PATH})
  else (ADDRESS_SIZE EQUAL 32)
    set(DEB_ARCHITECTURE amd64)
    set(FIND_LIBRARY_USE_LIB64_PATHS ON)
  endif (ADDRESS_SIZE EQUAL 32)

  execute_process(COMMAND dpkg-architecture -a${DEB_ARCHITECTURE} -qDEB_HOST_MULTIARCH
      RESULT_VARIABLE DPKG_RESULT
      OUTPUT_VARIABLE DPKG_ARCH
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  #message (STATUS "DPKG_RESULT ${DPKG_RESULT}, DPKG_ARCH ${DPKG_ARCH}")
  if (DPKG_RESULT EQUAL 0)
    set(CMAKE_LIBRARY_ARCHITECTURE ${DPKG_ARCH})
    set(CMAKE_SYSTEM_LIBRARY_PATH /usr/lib/${DPKG_ARCH} /usr/local/lib/${DPKG_ARCH} ${CMAKE_SYSTEM_LIBRARY_PATH})
  endif (DPKG_RESULT EQUAL 0)


  #if (INSTALL_PROPRIETARY)
    # Only turn on headless if we can find osmesa libraries.
    # include(FindPkgConfig)
    #pkg_check_modules(OSMESA osmesa)
    #if (OSMESA_FOUND)
    #  set(BUILD_HEADLESS ON CACHE BOOL "Build headless libraries.")
    #endif (OSMESA_FOUND)
  #endif (INSTALL_PROPRIETARY)

endif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(DARWIN 1)
  set(CMAKE_MACOSX_RPATH TRUE)

  # Xcode setup
  if (XCODE_VERSION LESS 12.0.0)
    message( FATAL_ERROR "Xcode 12.0.0 or greater is required." )
  endif ()
  message( STATUS "Building with " ${CMAKE_OSX_SYSROOT} )
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)

  set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS YES)
  set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT dwarf-with-dsym)

  # Obj-C
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK YES)

  # Disable codesigning, for now it's handled with snake
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "")

  set(CMAKE_XCODE_ATTRIBUTE_DISABLE_MANUAL_TARGET_ORDER_BUILD_WARNING YES)
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO)

  set(ADDRESS_SIZE 64)
  if(BUILD_ARM64)
    set(ARCH arm64)
    set(CMAKE_OSX_ARCHITECTURES arm64)
  elseif(BUILD_UNIVERSAL)
    set(ARCH universal)
    set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)
  else()
    set(ARCH x86_64)
    set(CMAKE_OSX_ARCHITECTURES x86_64)
  endif()
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

# Default deploy grid
set(GRID agni CACHE STRING "Target Grid")

set(ENABLE_SIGNING OFF CACHE BOOL "Enable signing the viewer")
set(SIGNING_IDENTITY "" CACHE STRING "Specifies the signing identity to use, if necessary.")

if (DEFINED ENV{VIEWER_ENABLE_SIGNING})
  set(ENABLE_SIGNING $ENV{VIEWER_ENABLE_SIGNING} CACHE BOOL "" FORCE)
endif()

if(DEFINED ENV{VIEWER_SIGNING_IDENTITY})
  set(SIGNING_IDENTITY $ENV{VIEWER_SIGNING_IDENTITY} CACHE STRING "Specifies the signing identity to use, if necessary." FORCE)
endif()

set(VERSION_BUILD "0" CACHE STRING "Revision number passed in from the outside")

set(USE_PRECOMPILED_HEADERS ON CACHE BOOL "Enable use of precompiled header directives where supported.")

set(VIEWER_UPDATE_SERVICE "" CACHE STRING "Update service URL")

source_group("CMake Rules" FILES CMakeLists.txt)

get_property(LL_GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

