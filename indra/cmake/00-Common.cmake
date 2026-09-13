# -*- cmake -*-
#
# Compiler, preprocessor and linker settings shared by every target in the
# tree, carried by the al::flags interface target. Organised by decision, not
# by platform: each section states one fact once and then says how each
# toolchain spells it.
#
# Every buildable target links al::flags; al_check_flags_target() at the end
# of the root CMakeLists.txt fails the configure if one does not. A target
# that needs to deviate does not append a competing option -- interface
# options come after a target's own on the command line, and the compiler
# takes the last of a repeated switch -- it sets one of the properties below,
# which al::flags reads for that target:
#
#   AL_SKIP_RELEASE_DEBUG_INFO   link Release without debug information
#
# Configurations:
#   Debug          - no optimisation, debug CRT, debug third-party libraries
#   OptDebug       - no optimisation, release CRT and third-party libraries
#   RelWithDebInfo - optimised, asserts on, symbols
#   Release        - optimised, asserts off, symbols kept aside for crash reports

include_guard()

include(Variables)
include(Linking)

set(AL_OPTIMIZED_CONFIGS "RelWithDebInfo,Release")

add_library(al_flags INTERFACE)
add_library(al::flags ALIAS al_flags)

#------------------------------------------------------------------------------
# Language and toolchain policy
#------------------------------------------------------------------------------
# These are target properties CMake initialises from variables; the variable is
# the sanctioned way to set them for a whole tree.

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # C++20 module scanning; unused here and slow

set(CMAKE_OPTIMIZE_DEPENDENCIES ON)  # static libraries do not wait on their dependencies' links
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(AL_USE_LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(WINDOWS)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_RUNTIME_CHECKS "$<$<CONFIG:Debug>:StackFrameErrorCheck;UninitializedVariable>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT $<IF:$<CONFIG:Debug,OptDebug>,EditAndContinue,ProgramDatabase>)
endif()

#------------------------------------------------------------------------------
# Configurations
#------------------------------------------------------------------------------

# OptDebug compiles like Debug and links like Release: the release CRT on
# Windows, and the release variant of every imported library.
if(LL_GENERATOR_IS_MULTI_CONFIG OR CMAKE_BUILD_TYPE STREQUAL "OptDebug")
  set(CMAKE_C_FLAGS_OPTDEBUG "${CMAKE_C_FLAGS_DEBUG}")
  set(CMAKE_CXX_FLAGS_OPTDEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  set(CMAKE_EXE_LINKER_FLAGS_OPTDEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
  set(CMAKE_MODULE_LINKER_FLAGS_OPTDEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG}")
  set(CMAKE_SHARED_LINKER_FLAGS_OPTDEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
  set(CMAKE_STATIC_LINKER_FLAGS_OPTDEBUG "${CMAKE_STATIC_LINKER_FLAGS_DEBUG}")
  if(WINDOWS)
    set(CMAKE_MAP_IMPORTED_CONFIG_OPTDEBUG Release)
  endif()
endif()

#------------------------------------------------------------------------------
# Sanitizers
#------------------------------------------------------------------------------

# AL_SANITIZERS is a list drawn from address, undefined and thread. GCC and
# Clang only. A sanitized build cannot link libwebrtc and trips warnings that
# are false positives, so it disables both.
set(AL_SANITIZING OFF)
if(AL_SANITIZERS AND (LINUX OR DARWIN))
  set(AL_SANITIZING ON)
  set(AL_USE_WEBRTC OFF)
  foreach(sanitizer IN LISTS AL_SANITIZERS)
    if(NOT sanitizer MATCHES "^(address|undefined|thread)$")
      message(FATAL_ERROR "AL_SANITIZERS: unknown sanitizer '${sanitizer}' (address, undefined, thread)")
    endif()
    target_compile_options(al_flags INTERFACE -fsanitize=${sanitizer})
    target_link_options(al_flags INTERFACE -fsanitize=${sanitizer})
  endforeach()
  target_compile_options(al_flags INTERFACE
    -U_FORTIFY_SOURCE
    -fno-omit-frame-pointer
    -fno-common
    -fsanitize-recover=all
  )
endif()

#------------------------------------------------------------------------------
# Diagnostics
#------------------------------------------------------------------------------

if(AL_ENABLE_WARNINGS_AS_ERRORS AND NOT AL_SANITIZING)
  set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

if(WINDOWS)
  target_compile_options(al_flags INTERFACE /W3)
elseif(LINUX OR DARWIN)
  target_compile_options(al_flags INTERFACE
    -Wall
    -Wno-sign-compare
    -Wno-trigraphs
    -Wno-reorder
    -Wno-unused-but-set-variable
    -Wno-unused-variable
  )
  if(COMPILER_IS_CLANG)
    target_compile_options(al_flags INTERFACE
      -Wno-unused-private-field
      -Wno-unused-local-typedef
      -Wno-reorder-ctor
    )
  elseif(COMPILER_IS_GCC)
    target_compile_options(al_flags INTERFACE
      -Wstrict-aliasing=2
      -Wno-stringop-truncation
      -Wno-stringop-overflow
      -Wno-parentheses
      -Wno-maybe-uninitialized
      -Wno-unused-local-typedefs
      -Wno-array-bounds  # false positives, including on libstdc++'s own headers
      -Wno-switch
    )
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
      target_compile_options(al_flags INTERFACE -Wno-sfinae-incomplete)
    endif()
  endif()
endif()

#------------------------------------------------------------------------------
# Optimisation and code generation
#------------------------------------------------------------------------------

# Optimisation level per configuration. CMake's own defaults cover Debug
# (/Od, -O0) and Release (/O2, -O3); OptDebug inherits Debug's flags and
# wants a little optimisation on GCC and Clang, and RelWithDebInfo wants
# -O3 where CMake gives -O2.
if(LINUX OR DARWIN)
  target_compile_options(al_flags INTERFACE
    $<$<CONFIG:OptDebug>:-Og>
    $<$<CONFIG:RelWithDebInfo>:-O3>
  )
endif()

# Inlining. MSVC's optimised defaults are /Ob1 (RelWithDebInfo) and /Ob2
# (Release); /Ob3 inlines aggressively enough to match GCC and Clang at -O3.
# Rewriting the configuration flags rather than appending keeps the project
# files truthful: an appended /Ob3 would override an /Ob1 the IDE still shows.
if(WINDOWS)
  foreach(lang C CXX)
    foreach(config RELWITHDEBINFO RELEASE)
      string(REGEX REPLACE "/Ob[0-9]" "/Ob3" CMAKE_${lang}_FLAGS_${config} "${CMAKE_${lang}_FLAGS_${config}}")
      if(NOT CMAKE_${lang}_FLAGS_${config} MATCHES "/Ob3")
        string(APPEND CMAKE_${lang}_FLAGS_${config} " /Ob3")
      endif()
    endforeach()
  endforeach()
endif()

# Floating point. Optimised builds take MSVC's /fp:fast contract on every
# compiler: reassociation, contraction and reciprocal transforms allowed,
# signed zero, FP exceptions and errno not guaranteed -- and NaN and
# infinity still treated as values that occur. GCC and Clang get that as
# the component flags rather than -ffast-math, which adds
# -ffinite-math-only (folding every isnan/isinf guard in the renderer to a
# constant) and, on the link line, crtfastmath.o (FTZ/DAZ for every thread
# in the process, including ones that are not ours). Compile options never
# reach the link line, and these spellings never trigger crtfastmath.o.
if(WINDOWS)
  target_compile_options(al_flags INTERFACE $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:/fp:fast>)
elseif(LINUX OR DARWIN)
  target_compile_options(al_flags INTERFACE
    -fno-math-errno
    $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:-ffp-contract=fast>
    $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:-fno-trapping-math>
    $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:-fno-signed-zeros>
    $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:-fassociative-math>
    $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:-freciprocal-math>
  )
endif()

# Instruction set: the same table the triplets read, so the viewer and its
# ports are built for one machine. AlchemyTarget.cmake says what each tier
# means and why macOS x86_64 ignores it.
include(AlchemyTarget)
al_isa_flags(${AL_ISA_TIER} ${CMAKE_SYSTEM_NAME} ${ARCH} al_isa_compile_flags)
target_compile_options(al_flags INTERFACE ${al_isa_compile_flags})

# Hardening.
if(LINUX)
  target_compile_options(al_flags INTERFACE $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:-fstack-protector>)
  if(NOT AL_SANITIZING)
    target_compile_definitions(al_flags INTERFACE $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>)
  endif()
endif()

# Toolchain conformance and miscellany.
if(WINDOWS)
  target_compile_options(al_flags INTERFACE
    /utf-8            # source and execution character sets, as on the other platforms
    /bigobj           # generated template code exceeds the default section limit
    /Gy               # function-level linking in every configuration, not only under /O2
    /Zc:wchar_t       # documented as the default, but without it link.exe reports corrupted
                      # type records (LNK4020) in the PDB of objects built against the shared PCH
    /MP
    /permissive-
    /Zc:preprocessor
    /Zc:__cplusplus
    /Zc:inline
  )
elseif(LINUX)
  target_compile_options(al_flags INTERFACE -fsigned-char)
elseif(DARWIN)
  # The Xcode attribute is what the Xcode generator honours; the compile
  # option covers Ninja and Makefile generators.
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
  target_compile_options(al_flags INTERFACE $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>)
endif()

#------------------------------------------------------------------------------
# Debug information
#------------------------------------------------------------------------------

# Every configuration carries symbols. CMake's Debug and RelWithDebInfo flags
# already say -g (and OptDebug inherits Debug's); Release keeps symbols too,
# for crash reports, and the packaging step strips them from the shipped
# binary. Windows sets the format through CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
# above, and links with full debug information unless the target opts out of
# it for Release through AL_SKIP_RELEASE_DEBUG_INFO.
if(LINUX OR DARWIN)
  target_compile_options(al_flags INTERFACE $<$<CONFIG:Release>:-g>)
elseif(WINDOWS)
  target_link_options(al_flags INTERFACE
    $<IF:$<AND:$<CONFIG:Release>,$<BOOL:$<TARGET_PROPERTY:AL_SKIP_RELEASE_DEBUG_INFO>>>,/DEBUG:NONE,/DEBUG:FULL>
  )
endif()

#------------------------------------------------------------------------------
# Preprocessor definitions
#------------------------------------------------------------------------------

# Per configuration. CMake's RelWithDebInfo and Release flags define NDEBUG
# themselves; OptDebug inherits Debug's flags and must define it here.
target_compile_definitions(al_flags INTERFACE
  $<$<CONFIG:Debug>:LL_DEBUG=1>
  $<$<CONFIG:Debug>:_DEBUG>
  $<$<CONFIG:OptDebug>:LL_DEBUG=1>
  $<$<CONFIG:OptDebug>:LL_OPTDEBUG=1>
  $<$<CONFIG:OptDebug>:NDEBUG>
  $<$<CONFIG:RelWithDebInfo>:LL_RELEASE=1>
  $<$<CONFIG:RelWithDebInfo>:LL_RELEASE_WITH_DEBUG_INFO=1>
  $<$<CONFIG:Release>:LL_RELEASE=1>
  $<$<CONFIG:Release>:LL_RELEASE_FOR_DOWNLOAD=1>
)

# Portable.
target_compile_definitions(al_flags INTERFACE
  ADDRESS_SIZE=${ADDRESS_SIZE}
  BOOST_BIND_GLOBAL_PLACEHOLDERS       # Boost.Bind's _1, _2 in the global namespace, which the code relies on
  GLM_FORCE_DEFAULT_ALIGNED_GENTYPES=1 # SIMD-aligned GLM types; https://github.com/g-truc/glm/blob/master/manual.md#section2_10
  GLM_ENABLE_EXPERIMENTAL=1
  SSE2NEON_SUPPRESS_WARNINGS=1         # SSE2NEON warns under optimisation for no reason
)

if(AL_ENABLE_CRASH_REPORTING)
  target_compile_definitions(al_flags INTERFACE LL_SEND_CRASH_REPORTS=1)
endif()

if(NOT AL_ENABLE_RELEASE_DEBUG_LOGGING)
  target_compile_definitions(al_flags INTERFACE $<$<CONFIG:Release>:LL_DISABLE_DEBUG_LOGGING=1>)
endif()

# Per platform.
if(WINDOWS)
  target_compile_definitions(al_flags INTERFACE
    LL_WINDOWS=1
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS          # sprintf and friends
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_OBSOLETE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
  )
elseif(LINUX)
  target_compile_definitions(al_flags INTERFACE
    LL_LINUX=1
    _REENTRANT
    APPID=secondlife
    LL_IGNORE_SIGCHLD  # third-party libraries install their own SIGCHLD handlers; the viewer needs none
  )
elseif(DARWIN)
  target_compile_definitions(al_flags INTERFACE
    LL_DARWIN=1
    GL_SILENCE_DEPRECATION=1
  )
endif()

#------------------------------------------------------------------------------
# Linking
#------------------------------------------------------------------------------

if(WINDOWS)
  target_link_options(al_flags INTERFACE
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
    /LARGEADDRESSAWARE
    $<$<CONFIG:OptDebug,RelWithDebInfo,Release>:/NODEFAULTLIB:LIBCMTD>
    $<$<CONFIG:Debug>:/NODEFAULTLIB:LIBCMT>
  )
elseif(LINUX)
  target_link_options(al_flags INTERFACE
    "LINKER:-z,relro"
    "LINKER:-z,now"
    "LINKER:--build-id"
    "LINKER:--as-needed"
    "LINKER:--no-undefined"
  )
elseif(DARWIN)
  target_link_options(al_flags INTERFACE
    $<$<CONFIG:${AL_OPTIMIZED_CONFIGS}>:LINKER:-dead_strip>
    LINKER:-dead_strip_dylibs
    "LINKER:-headerpad_max_install_names"
    "LINKER:-search_paths_first"
  )
  # ld64 needs somewhere to keep LTO intermediates or dsymutil finds no symbols.
  if(AL_USE_LTO AND NOT XCODE)
    target_link_options(al_flags INTERFACE
      "LINKER:-cache_path_lto,${CMAKE_BINARY_DIR}/LTOCache"
      "LINKER:-object_path_lto,$<TARGET_PROPERTY:BINARY_DIR>/CMakeFiles/$<TARGET_PROPERTY:NAME>.dir/$<IF:$<BOOL:${LL_GENERATOR_IS_MULTI_CONFIG}>,$<CONFIG>/,>$<TARGET_PROPERTY:NAME>_lto.o"
    )
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/LTOCache")
  endif()
endif()

#------------------------------------------------------------------------------
# Xcode generator
#------------------------------------------------------------------------------

if(DARWIN)
  set(CMAKE_XCODE_GENERATE_TOP_LEVEL_PROJECT_ONLY ON)
  set(CMAKE_XCODE_GENERATE_SCHEME ON)
  set(CMAKE_XCODE_SCHEME_LAUNCH_CONFIGURATION "RelWithDebInfo")
  if("address" IN_LIST AL_SANITIZERS)
    set(CMAKE_XCODE_SCHEME_ADDRESS_SANITIZER ON)
  endif()
  if("undefined" IN_LIST AL_SANITIZERS)
    set(CMAKE_XCODE_SCHEME_UNDEFINED_BEHAVIOUR_SANITIZER ON)
  endif()
  if("thread" IN_LIST AL_SANITIZERS)
    set(CMAKE_XCODE_SCHEME_THREAD_SANITIZER ON)
  endif()

  set(CMAKE_XCODE_ATTRIBUTE_COMPILATION_CACHE_ENABLE_CACHING YES)
  set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS YES)
  set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf") # dSYMs only where a target asks
  set(CMAKE_XCODE_ATTRIBUTE_GCC_FAST_MATH NO)
  # Xcode does not read -march; this is the Darwin row of AlchemyTarget.cmake
  # in the spelling it does read.
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_X86_VECTOR_INSTRUCTIONS sse4.2)
  # Xcode's own signing cannot handle the embedded CEF bundles; signing is an
  # install step (ViewerCodeSign.cmake). Since Xcode 14.1 all three are needed
  # to stop it signing implicitly. https://stackoverflow.com/a/54296008
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "")
  set(CMAKE_XCODE_ATTRIBUTE_DISABLE_MANUAL_TARGET_ORDER_BUILD_WARNING YES)
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO)
endif()

#------------------------------------------------------------------------------
# The guard
#------------------------------------------------------------------------------

# Fails the configure if any buildable target in the tree does not link
# al::flags. Called from the root after the last add_subdirectory().
function(_al_collect_targets dir out)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(sub IN LISTS subdirs)
    _al_collect_targets("${sub}" sub_targets)
    list(APPEND targets ${sub_targets})
  endforeach()
  set(${out} "${targets}" PARENT_SCOPE)
endfunction()

function(al_check_flags_target)
  _al_collect_targets("${CMAKE_SOURCE_DIR}" targets)
  set(missing)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(NOT type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
      continue()
    endif()
    get_target_property(libs ${target} LINK_LIBRARIES)
    if(NOT libs OR NOT ("al::flags" IN_LIST libs OR "al_flags" IN_LIST libs))
      list(APPEND missing ${target})
    endif()
  endforeach()
  if(missing)
    list(JOIN missing ", " missing_text)
    message(FATAL_ERROR "These targets do not link al::flags: ${missing_text}")
  endif()
endfunction()
