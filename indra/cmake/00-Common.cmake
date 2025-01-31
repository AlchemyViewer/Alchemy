# -*- cmake -*-
#
# Compilation options shared by all Second Life components.

#*****************************************************************************
#   It's important to realize that CMake implicitly concatenates
#   CMAKE_CXX_FLAGS with (e.g.) CMAKE_CXX_FLAGS_RELEASE for Release builds. So
#   set switches in CMAKE_CXX_FLAGS that should affect all builds, but in
#   CMAKE_CXX_FLAGS_RELEASE or CMAKE_CXX_FLAGS_RELWITHDEBINFO for switches
#   that should affect only that build variant.
#
#   Also realize that CMAKE_CXX_FLAGS may already be partially populated on
#   entry to this file.
#*****************************************************************************
include_guard()

include(CheckCXXCompilerFlag)
include(Variables)
include(SDL2)

# Portable compilation flags.
add_compile_definitions(
    ADDRESS_SIZE=${ADDRESS_SIZE}
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:LL_DEBUG=1>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:LL_RELEASE=1>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:LL_RELEASE_WITH_DEBUG_INFO=1>
    $<$<CONFIG:Release>:LL_RELEASE_FOR_DOWNLOAD=1>
    )
# Configure crash reporting
set(RELEASE_CRASH_REPORTING OFF CACHE BOOL "Enable use of crash reporting in release builds")
set(NON_RELEASE_CRASH_REPORTING OFF CACHE BOOL "Enable use of crash reporting in developer builds")

if(RELEASE_CRASH_REPORTING)
  add_compile_definitions( LL_SEND_CRASH_REPORTS=1)
endif()

if(NON_RELEASE_CRASH_REPORTING)
  add_compile_definitions( LL_SEND_CRASH_REPORTS=1)
endif()

# Don't bother with a MinSizeRel build.
set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo;Release;Debug" CACHE STRING
    "Supported build types." FORCE)

# SIMD config
option(USE_AVX2 "Enable usage of the AVX2 instruction set" OFF)
option(USE_AVX "Enable usage of the AVX instruction set" OFF)
option(USE_SSE42 "Enable usage of the SSE4.2 instruction set" ON)

# Warnings
option(DISABLE_FATAL_WARNINGS "Disable warnings as errors" ON)

if(USE_LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${USE_LTO})
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET "hidden")
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Platform-specific compilation flags.
if (WINDOWS)
  # Don't build DLLs.
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_compile_options(/MP)
  elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    add_compile_options(-m${ADDRESS_SIZE})
  endif ()

  add_compile_definitions(
    $<$<CONFIG:Debug>:_SCL_SECURE_NO_WARNINGS=1>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:_ITERATOR_DEBUG_LEVEL=0>
    LL_WINDOWS=1
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _SILENCE_CXX20_CISO646_REMOVED_WARNING
    )

  add_compile_options(
    $<$<CONFIG:Release>:/Zc:inline>
    $<$<CONFIG:Release>:/fp:fast>
    /Zi
    /EHsc
    /permissive-
    /W3
    /c
    /Zc:__cplusplus
    /Zc:preprocessor
    /Zc:char8_t-
    /nologo
    )

  add_link_options(
    /DEBUG:FULL
    /IGNORE:4099
    )

  if (ADDRESS_SIZE EQUAL 32)
    add_compile_options(/arch:SSE2)
  elseif (USE_AVX2)
    add_compile_options(/arch:AVX2)
  elseif (USE_AVX)
    add_compile_options(/arch:AVX)
  elseif (USE_SSE42)
    add_compile_options(/arch:SSE4.2)
    add_compile_definitions(__SSE3__=1 __SSSE3__=1 __SSE4__=1 __SSE4_1__=1 __SSE4_2__=1)
  else ()
    add_compile_definitions(__SSE3__=1)
  endif ()

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_compile_options(/Zc:externConstexpr /ZH:SHA_256)
  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    add_compile_options(/Qvec /Zc:dllexportInlines- /clang:-mprefer-vector-width=128 -fno-strict-aliasing -Wno-ignored-pragma-intrinsic -Wno-unused-local-typedef)
  endif()

  if(FAVOR_AMD AND FAVOR_INTEL)
      message(FATAL_ERROR "Cannot enable FAVOR_AMD and FAVOR_INTEL at the same time")
  elseif(FAVOR_AMD)
      add_compile_options(/favor:AMD64)
  elseif(FAVOR_INTEL)
      add_compile_options(/favor:INTEL64)
  endif()

  if (USE_LTO)
    add_link_options(
      $<$<CONFIG:Release>:/OPT:REF>
      $<$<CONFIG:Release>:/OPT:ICF>
      )
  elseif (INCREMENTAL_LINK)
    add_link_options($<$<CONFIG:Release>:/INCREMENTAL>)
  else ()
    add_link_options(
      $<$<CONFIG:Release>:/OPT:REF>
      $<$<CONFIG:Release>:/OPT:ICF>
      $<$<CONFIG:Release>:/INCREMENTAL:NO>
      )
  endif ()

  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    # This is a massive hack and makes me sad. clang-cl fails to find its own builtins library :/ x64 only for now.
    set(CLANG_RT_NAMES clang_rt.builtins-x86_64)
    find_library(CLANG_RT NAMES ${CLANG_RT_NAMES}
                PATHS [HKEY_LOCAL_MACHINE\\SOFTWARE\\LLVM\\LLVM]/lib/clang/${CMAKE_CXX_COMPILER_VERSION}/lib/windows
                [HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\LLVM\\LLVM]/lib/clang/${CMAKE_CXX_COMPILER_VERSION}/lib/windows)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /defaultlib:\"${CLANG_RT}\"")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /defaultlib:\"${CLANG_RT}\"")
  endif()

  if (NOT DISABLE_FATAL_WARNINGS)
    add_compile_options(/WX)
  endif (NOT DISABLE_FATAL_WARNINGS)

  string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
  string(REPLACE "/Ob2" "/Ob3" CMAKE_C_FLAGS_RELEASE ${CMAKE_C_FLAGS_RELEASE})

  # configure win32 API for 10 and above compatibility
  set(WINVER "0x0A00" CACHE STRING "Win32 API Target version (see http://msdn.microsoft.com/en-us/library/aa383745%28v=VS.85%29.aspx)")
  add_compile_definitions(WINVER=${WINVER} _WIN32_WINNT=${WINVER})
endif (WINDOWS)

if (LINUX)
  set(CMAKE_SKIP_BUILD_RPATH TRUE)

  add_compile_definitions(
    LL_LINUX=1
    APPID=secondlife
    LL_IGNORE_SIGCHLD
    _REENTRANT
    $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>
    )

  add_compile_options(
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fstack-protector>
    -fexceptions
    -fno-math-errno
    -fno-strict-aliasing
    -fno-omit-frame-pointer
    -fsigned-char
    -g
    -gz
    -pthread
    -fdiagnostics-color=always
    )

  if (USE_AVX2)
    add_compile_options(-mavx2)
  elseif (USE_AVX)
    add_compile_options(-mavx)
  elseif (USE_SSE42)
    add_compile_options(-mfpmath=sse -msse -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2)
  else()
    add_compile_options(-mfpmath=sse -msse -msse2 -msse3 -mssse3 -msse4 -msse4.1)
  endif ()

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if (USE_ASAN)
      add_compile_options(-fsanitize=address -fsanitize-recover=address)
      link_libraries(-lasan)
    elseif (USE_LEAKSAN)
      add_compile_options(-fsanitize=leak)
      link_libraries(-llsan)
    elseif (USE_UBSAN)
      add_compile_options(-fsanitize=undefined -fno-sanitize=vptr)
      link_libraries(-lubsan)
    elseif (USE_THDSAN)
      add_compile_options(-fsanitize=thread)
    endif ()
  endif ()

  if (USE_ASAN OR USE_LEAKSAN OR USE_UBSAN OR USE_THDSAN)
    add_compile_options(-Og)
  else ()
    add_compile_options(-O3)
  endif ()

  # Enable these flags so we have a read only GOT and some linking opts
  add_link_options("LINKER:-z,relro" "LINKER:-z,now" "LINKER:--as-needed" "LINKER:--build-id=uuid")
endif ()

if (DARWIN)
  add_compile_definitions(LL_DARWIN=1 GL_SILENCE_DEPRECATION=1)
  add_link_options("LINKER:-headerpad_max_install_names" "LINKER:-search_paths_first" "LINKER:-dead_strip")
  add_compile_options(
    -O3
    -gdwarf-2
    -fno-strict-aliasing
    -msse4.2
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-weak>
    )
endif ()

if (LINUX OR DARWIN)
  add_compile_options(-Wall -Wno-sign-compare -Wno-reorder)
  if (${CMAKE_CXX_COMPILER_ID} STREQUAL GNU)
    add_compile_options(-Wno-unused-parameter -Wno-unused-but-set-parameter -Wno-ignored-qualifiers -Wno-unused-function)
  elseif (${CMAKE_CXX_COMPILER_ID} MATCHES Clang)
    add_compile_options(-Wno-trigraphs -Wno-unused-local-typedef -Wno-unknown-warning-option -Wno-shorten-64-to-32)
  endif()

  CHECK_CXX_COMPILER_FLAG(-Wdeprecated-copy HAS_DEPRECATED_COPY)
  if (HAS_DEPRECATED_COPY)
    add_compile_options(-Wno-deprecated-copy)
  endif()

  if (NOT DISABLE_FATAL_WARNINGS)
    add_compile_options(-Werror)
  endif (NOT DISABLE_FATAL_WARNINGS)

  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    add_compile_options(-Wno-unused-but-set-variable -Wno-unused-variable )
  endif()

  add_compile_options(-m${ADDRESS_SIZE})
endif ()

option(RELEASE_SHOW_ASSERTS "Enable asserts in release builds" OFF)

if(RELEASE_SHOW_ASSERTS)
  add_compile_definitions(RELEASE_SHOW_ASSERT=1)
endif()

option(ENABLE_TIMING "Enable all fast timers" ON)
if(ENABLE_TIMING)
  add_compile_definitions(AL_ENABLE_ALL_TIMERS=1)
endif()

if(HAVOK OR HAVOK_TPV)
  add_compile_definitions(LL_HAVOK=1)
endif()
