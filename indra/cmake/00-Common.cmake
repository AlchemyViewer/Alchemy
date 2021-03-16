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

if(NOT DEFINED ${CMAKE_CURRENT_LIST_FILE}_INCLUDED)
set(${CMAKE_CURRENT_LIST_FILE}_INCLUDED "YES")

include(CheckCXXCompilerFlag)
include(Variables)

# Portable compilation flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DADDRESS_SIZE=${ADDRESS_SIZE}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DADDRESS_SIZE=${ADDRESS_SIZE}")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -DLL_DEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE
    "-DLL_RELEASE=1 -DLL_RELEASE_FOR_DOWNLOAD=1 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
    "-DLL_RELEASE=1 -DNDEBUG -DLL_RELEASE_WITH_DEBUG_INFO=1")
# Configure crash reporting
set(RELEASE_CRASH_REPORTING OFF CACHE BOOL "Enable use of crash reporting in release builds")
set(NON_RELEASE_CRASH_REPORTING OFF CACHE BOOL "Enable use of crash reporting in developer builds")

if(RELEASE_CRASH_REPORTING)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DLL_SEND_CRASH_REPORTS=1")
endif()

if(NON_RELEASE_CRASH_REPORTING)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DLL_SEND_CRASH_REPORTS=1")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DLL_SEND_CRASH_REPORTS=1")
endif()  

# Don't bother with a MinSizeRel build.
set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo;Release;Debug" CACHE STRING
    "Supported build types." FORCE)

# SIMD config
option(USE_SSE41 "Enable usage of the SSE4.2 instruction set" OFF)
option(USE_AVX "Enable usage of the AVX instruction set" OFF)
option(USE_AVX2 "Enable usage of the AVX2 instruction set" OFF)
if((USE_SSE41 AND USE_AVX) OR (USE_SSE41 AND USE_AVX AND USE_AVX2) OR (USE_AVX AND USE_AVX2))
  message(FATAL_ERROR "Usage of multiple SIMD flags is unsupported")
endif()

# Platform-specific compilation flags.
if (WINDOWS)
  # Don't build DLLs.
  set(BUILD_SHARED_LIBS OFF)

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m${ADDRESS_SIZE}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m${ADDRESS_SIZE}")
  endif ()

  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi /MDd /EHsc -D_SCL_SECURE_NO_WARNINGS=1")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
      "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Zi /MD /Ob0 /EHsc -D_ITERATOR_DEBUG_LEVEL=0")

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS_RELEASE
        "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /Ot /Gy /Zi /MD /Ob3 /Oy- /Zc:inline /EHsc /fp:fast -D_ITERATOR_DEBUG_LEVEL=0")
  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_RELEASE
        "${CMAKE_CXX_FLAGS_RELEASE} /clang:-Ofast /clang:-ffast-math /Oi /Ot /Gy /Zi /MD /Ob2 /Oy- /Zc:inline /EHsc /fp:fast -D_ITERATOR_DEBUG_LEVEL=0")
  endif()

  if (ADDRESS_SIZE EQUAL 32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LARGEADDRESSAWARE")
  endif (ADDRESS_SIZE EQUAL 32)

  if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR FULL_DEBUG_SYMS OR USE_CRASHPAD)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG:FULL")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG:FULL")
  else ()
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG:FASTLINK /IGNORE:4099")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG:FASTLINK /IGNORE:4099")
  endif ()

  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:LIBCMT")
  set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:LIBCMT /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:MSVCRT")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:LIBCMT")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:LIBCMT /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:MSVCRT")
  
  if (USE_LTO)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
      if(INCREMENTAL_LINK)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG:incremental")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG:incremental")
        set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /LTCG")
      else(INCREMENTAL_LINK)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG")
        set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /LTCG")
      endif(INCREMENTAL_LINK)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /OPT:REF /OPT:ICF /INCREMENTAL:NO")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /OPT:REF /OPT:ICF /INCREMENTAL:NO")
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL /Gy /Gw")
    elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
      if(INCREMENTAL_LINK)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=thin -fwhole-program-vtables /clang:-fforce-emit-vtables")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=thin -fwhole-program-vtables /clang:-fforce-emit-vtables")
      else(INCREMENTAL_LINK)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=full -fwhole-program-vtables /clang:-fforce-emit-vtables")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=full -fwhole-program-vtables /clang:-fforce-emit-vtables")
      endif(INCREMENTAL_LINK)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /OPT:REF /OPT:ICF")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /OPT:REF /OPT:ICF")
    endif()
  elseif (INCREMENTAL_LINK)
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /INCREMENTAL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /INCREMENTAL")
  else ()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /OPT:REF /OPT:ICF /INCREMENTAL:NO")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /OPT:REF /OPT:ICF /INCREMENTAL:NO")
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

  set(GLOBAL_CXX_FLAGS 
      "/GS /W3 /c /Zc:__cplusplus  /Zc:forScope /Zc:rvalueCast /Zc:strictStrings /Zc:ternary /nologo"
      )

  if (USE_AVX2)
    set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /arch:AVX2")
    add_definitions(/DAL_AVX2=1 /DAL_AVX=1)
  elseif (USE_AVX)
    set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /arch:AVX")
    add_definitions(/DAL_AVX=1)
  elseif (USE_SSE41)
    add_definitions(/D__SSE3__=1 /D__SSSE3__=1 /D__SSE4__=1 /D__SSE4_1__=1)
  elseif (ADDRESS_SIZE EQUAL 32)
    set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /arch:SSE2")
  endif ()

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /permissive- /Zc:externConstexpr /Zc:referenceBinding")
  elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /Qvec /Zc:dllexportInlines- /clang:-mprefer-vector-width=128 -fno-strict-aliasing -Wno-ignored-pragma-intrinsic -Wno-unused-local-typedef")
  endif()

  if(FAVOR_AMD AND FAVOR_INTEL)
      message(FATAL_ERROR "Cannot enable FAVOR_AMD and FAVOR_INTEL at the same time")
  elseif(FAVOR_AMD)
      set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /favor:AMD64")
  elseif(FAVOR_INTEL)
      set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /favor:INTEL64")
  endif()

  if (NOT VS_DISABLE_FATAL_WARNINGS)
    set(GLOBAL_CXX_FLAGS "${GLOBAL_CXX_FLAGS} /WX")
  endif (NOT VS_DISABLE_FATAL_WARNINGS)

  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${GLOBAL_CXX_FLAGS}" CACHE STRING "C++ compiler debug options" FORCE)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${GLOBAL_CXX_FLAGS}" CACHE STRING "C++ compiler release-with-debug options" FORCE)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${GLOBAL_CXX_FLAGS}" CACHE STRING "C++ compiler release options" FORCE)

  add_definitions(
      /DLL_WINDOWS=1
      /DNOMINMAX
      /DUNICODE
      /D_UNICODE
      /D_CRT_SECURE_NO_WARNINGS
      /D_CRT_NONSTDC_NO_DEPRECATE
      /D_WINSOCK_DEPRECATED_NO_WARNINGS
      /D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING
      /D_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING
      /DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE
      /DBOOST_ALLOW_DEPRECATED_HEADERS
      )

  # library linkage defines
  add_definitions(
      /DBOOST_ALL_DYN_LINK
      /DDLL_IMPORT
      /DDOM_DYNAMIC
      /DPNG_USE_DLL
      /DWEBP_DLL
      /DZLIB_DLL
  )

  # configure win32 API for 7 and above compatibility
  set(WINVER "0x0601" CACHE STRING "Win32 API Target version (see http://msdn.microsoft.com/en-us/library/aa383745%28v=VS.85%29.aspx)")
  add_definitions("/DWINVER=${WINVER}" "/D_WIN32_WINNT=${WINVER}")
endif (WINDOWS)


if (LINUX)
  set(CMAKE_SKIP_RPATH TRUE)

  set(ALCHEMY_GLOBAL_DEFS "-DLL_LINUX=1 -DAPPID=secondlife -DLL_IGNORE_SIGCHLD -D_REENTRANT -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGSEAL_ENABLE -DGTK_DISABLE_SINGLE_INCLUDES")
  set(ALCHEMY_GLOBAL_CFLAGS "-fvisibility=hidden -fexceptions -fno-math-errno -fno-strict-aliasing -fsigned-char -g -pthread -msse4.2 -mfpmath=sse")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ALCHEMY_GLOBAL_DEFS} ${ALCHEMY_GLOBAL_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ALCHEMY_GLOBAL_DEFS} ${ALCHEMY_GLOBAL_CFLAGS}")

  if (USE_LTO)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=auto -fno-fat-lto-objects")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=auto -fno-fat-lto-objects")
  endif (USE_LTO)

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if (USE_ASAN)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-recover=address")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-recover=address")
      link_libraries(-lasan)
    endif (USE_ASAN)

    if (USE_LEAKSAN)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=leak")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
      link_libraries(-llsan)
    endif (USE_LEAKSAN)

    if (USE_UBSAN)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
      link_libraries(-lubsan)
    endif (USE_UBSAN)

    if (USE_THDSAN)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    endif (USE_THDSAN)
  endif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")

  CHECK_CXX_COMPILER_FLAG(-Og HAS_DEBUG_OPTIMIZATION)
  CHECK_CXX_COMPILER_FLAG(-fstack-protector-strong HAS_STRONG_STACK_PROTECTOR)
  CHECK_CXX_COMPILER_FLAG(-fstack-protector HAS_STACK_PROTECTOR)
  if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
    if (HAS_STRONG_STACK_PROTECTOR)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
    elseif (HAS_STACK_PROTECTOR)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector")
    endif (HAS_STRONG_STACK_PROTECTOR)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2")
  endif (${CMAKE_BUILD_TYPE} STREQUAL "Release")

  if (HAS_DEBUG_OPTIMIZATION)
    set(CMAKE_CXX_FLAGS_DEBUG "-Og ${CMAKE_CXX_FLAGS_DEBUG}")
  else (HAS_DEBUG_OPTIMIZATION)
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -fno-inline ${CMAKE_CXX_FLAGS_DEBUG}")
  endif (HAS_DEBUG_OPTIMIZATION)

  if (USE_ASAN OR USE_LEAKSAN OR USE_UBSAN OR USE_THDSAN)
    set(CMAKE_CXX_FLAGS_RELEASE "-Og ${CMAKE_CXX_FLAGS_RELEASE}")
  else ()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math ${CMAKE_CXX_FLAGS_RELEASE}")
  endif ()
  # Enable these flags so we have a read only GOT and some linking opts
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,now -Wl,--as-needed")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,now -Wl,--as-needed")
endif (LINUX)


if (DARWIN)
  # Warnings should be fatal -- thanks, Nicky Perian, for spotting reversed default
  set(CLANG_DISABLE_FATAL_WARNINGS OFF)
  add_definitions(-DLL_DARWIN=1 -DGL_SILENCE_DEPRECATION)
  set(CMAKE_CXX_LINK_FLAGS "-Wl,-headerpad_max_install_names,-search_paths_first")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_CXX_LINK_FLAGS}")
  set(DARWIN_extra_cstar_flags "-gdwarf-2 -Wno-unused-local-typedef")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DARWIN_extra_cstar_flags}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  ${DARWIN_extra_cstar_flags}")
  # NOTE: it's critical that the optimization flag is put in front.
  # NOTE: it's critical to have both CXX_FLAGS and C_FLAGS covered.

  add_definitions(-D_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR=1)
endif (DARWIN)


if (LINUX OR DARWIN)
  if (CMAKE_CXX_COMPILER MATCHES ".*clang")
    set(CMAKE_COMPILER_IS_CLANGXX 1)
  endif (CMAKE_CXX_COMPILER MATCHES ".*clang")

  if (CMAKE_COMPILER_IS_GNUCXX)
    set(GCC_WARNINGS "-Wall -Wno-sign-compare -Wno-unused-parameter -Wno-unused-but-set-parameter -Wno-ignored-qualifiers -Wno-unused-function -Wnon-virtual-dtor")
  elseif (CMAKE_COMPILER_IS_CLANGXX)
    set(GCC_WARNINGS "-Wall -Wno-sign-compare -Wno-trigraphs")
  endif()

  CHECK_CXX_COMPILER_FLAG(-Wdeprecated-copy HAS_DEPRECATED_COPY)
  if (HAS_DEPRECATED_COPY)
    set(GCC_WARNINGS "${GCC_WARNINGS} -Wno-deprecated-copy")
  endif()

  if (NOT GCC_DISABLE_FATAL_WARNINGS)
  #  set(GCC_WARNINGS "${GCC_WARNINGS} -Werror")
  endif (NOT GCC_DISABLE_FATAL_WARNINGS)

  set(GCC_CXX_WARNINGS "${GCC_WARNINGS} -Wno-reorder")

  set(CMAKE_C_FLAGS "${GCC_WARNINGS} ${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${GCC_CXX_WARNINGS} ${CMAKE_CXX_FLAGS}")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m${ADDRESS_SIZE}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m${ADDRESS_SIZE}")
endif (LINUX OR DARWIN)

add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)

if (USESYSTEMLIBS)
  add_definitions(-DLL_USESYSTEMLIBS=1)
endif (USESYSTEMLIBS)

option(RELEASE_SHOW_ASSERTS "Enable asserts in release builds" OFF)

if(RELEASE_SHOW_ASSERTS)
  add_definitions(-DRELEASE_SHOW_ASSERT=1)
else()
  add_definitions(-URELEASE_SHOW_ASSERT)
endif()

option(ENABLE_TIMING "Enable all fast timers" ON)
if(ENABLE_TIMING)
  add_definitions(-DAL_ENABLE_ALL_TIMERS=1)
else()
  add_definitions(-DAL_ENABLE_ALL_TIMERS=0)
endif()


endif(NOT DEFINED ${CMAKE_CURRENT_LIST_FILE}_INCLUDED)
