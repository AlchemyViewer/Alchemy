# -*- cmake -*-
#
# Reads the assembly the compiler wrote for llcommon/tests/alsimd_asm_probe.cpp
# and fails when an operation of the SIMD ops layer stopped being a straight
# instruction sequence: a byte table lookup where a permutation should be an
# ext, zip, uzp or rev; a call; or more instructions than the ceiling here
# allows for. Run as a test:
#
#   cmake -DOBJECT=<the probe's object file> -DSYNTAX=gnu|masm
#         -DARCH=x86|arm64 -DCOMPILER=<CMAKE_CXX_COMPILER_ID> -P AlSimdAsmCheck.cmake
#
# The listing sits beside the object: the .s that -save-temps=obj writes, or
# the .asm that /FAs writes. Its name follows the compiler, so it is found by
# a glob.

cmake_minimum_required(VERSION 4.0)

foreach(var OBJECT SYNTAX ARCH COMPILER)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "AlSimdAsmCheck: ${var} is required")
  endif()
endforeach()

get_filename_component(listing_dir "${OBJECT}" DIRECTORY)
file(GLOB listings "${listing_dir}/alsimd_asm_probe*.s" "${listing_dir}/alsimd_asm_probe*.asm")
list(LENGTH listings listing_count)
if(NOT listing_count EQUAL 1)
  message(FATAL_ERROR "AlSimdAsmCheck: expected one listing beside ${OBJECT}, found: ${listings}")
endif()
list(GET listings 0 listing)

# Instructions each probe may take, not counting the return: the largest
# count observed on any target plus room for a compiler to choose
# differently, but not for a table lookup with its load, a call, or a loop.
# The largest counts are the SSE2 forms below AVX, whose two-operand
# instructions need register copies; a permutation is one instruction on
# x86 and up to three on arm64.
# gersemi: off
set(
  ceilings
  load=3 loadu=3 load3=6 store=3 set=6 set1=3
  lane0=3 lane2=3 splat2=3
  shuffle_yzxw=5 shuffle_zxyw=5 shuffle_yxwz=4 shuffle_zwxy=4 shuffle_wwwx=5 shuffle2=5
  movelh=3 movehl=4 unpacklo=3 unpackhi=3
  add=3 sub=3 mul=3 div=3 neg=4 abs=4
  fmadd=4 fmsub=5 fnmadd=4 fmadd_lane1=4
  min=3 max=3 sqrt=3 rsqrt_fast=6 rsqrt=12 rcp_fast=5 rcp=8
  round=4 floor=8 ceil=9 cvt_round=3 cvt_trunc=3 to_float=3
  and=3 andnot=3 or=3 xor=3
  mask_lane2=4 mask_xyz=4 mask_not=4
  cmplt=3 cmpge=3 cmpeq=3 cmpne=4 nonfinite=6 select=5
  bits=7 any=6 all=6 any3=7 all3=8
  dot3=10 dot4=9 cross3=16 prefetch=3 transform=18
)
# gersemi: on

# MSVC for arm64 has no way to ask for an arbitrary permutation but a byte
# table, and no build ships from it; the rule against one holds for the
# compilers that choose the sequence themselves.
set(forbid_table TRUE)
if(ARCH STREQUAL "arm64" AND COMPILER STREQUAL "MSVC")
  set(forbid_table FALSE)
endif()

if(SYNTAX STREQUAL "gnu")
  set(function_start "^_?(probe_[a-z0-9_]+):")
  set(function_end "^\t(ret|retq)([ \t]|$)")
  set(instruction "^\t[a-z]")
  set(directive "^\t\\.")
elseif(SYNTAX STREQUAL "masm")
  # A |name| on arm64; name@@N under __vectorcall on x64.
  set(function_start "^\\|?(probe_[a-z0-9_]+)(@@[0-9]+)?\\|?[ \t]+PROC")
  set(function_end "(^|[ \t])ENDP([ \t]|$)")
  set(instruction "^\t[a-z]")
  set(directive "^\tnpad")
else()
  message(FATAL_ERROR "AlSimdAsmCheck: SYNTAX is gnu or masm, not ${SYNTAX}")
endif()

if(ARCH STREQUAL "x86")
  set(call "^\t(call|callq)[ \t]")
  set(table "^$")
elseif(ARCH STREQUAL "arm64")
  set(call "^\t(bl|blr)[ \t]")
  set(table "^\t(tbl|tbx)[0-9]?[ \t]")
else()
  message(FATAL_ERROR "AlSimdAsmCheck: ARCH is x86 or arm64, not ${ARCH}")
endif()

file(STRINGS "${listing}" lines)
set(current "")
set(count 0)
set(failures "")
set(seen "")

function(_al_finish_function name count)
  if(NOT name)
    return()
  endif()
  set(ceiling "")
  foreach(entry IN LISTS ceilings)
    if(entry MATCHES "^${name}=([0-9]+)$")
      set(ceiling ${CMAKE_MATCH_1})
    endif()
  endforeach()
  if(NOT ceiling)
    return()
  endif()
  if(count GREATER ceiling)
    list(APPEND failures "probe_${name}: ${count} instructions, ceiling ${ceiling}")
  endif()
  set(failures "${failures}" PARENT_SCOPE)
endfunction()

foreach(line IN LISTS lines)
  if(line MATCHES "${function_start}")
    _al_finish_function("${current}" ${count})
    string(REGEX REPLACE "^probe_" "" current "${CMAKE_MATCH_1}")
    list(APPEND seen ${current})
    set(count 0)
    continue()
  endif()
  if(NOT current)
    continue()
  endif()
  if(line MATCHES "${function_end}")
    _al_finish_function("${current}" ${count})
    set(current "")
    set(count 0)
    continue()
  endif()
  if(
    line MATCHES "${instruction}"
    AND NOT line MATCHES "${directive}"
    AND NOT line MATCHES "^\t(ret|retq)([ \t]|$)"
  )
    math(EXPR count "${count} + 1")
    if(line MATCHES "${call}")
      list(APPEND failures "probe_${current}: calls out: ${line}")
    endif()
    if(forbid_table AND line MATCHES "${table}")
      list(APPEND failures "probe_${current}: a table lookup: ${line}")
    endif()
  endif()
endforeach()
_al_finish_function("${current}" ${count})

foreach(entry IN LISTS ceilings)
  string(REGEX REPLACE "=.*$" "" name "${entry}")
  if(NOT name IN_LIST seen)
    list(APPEND failures "probe_${name}: not found in the listing")
  endif()
endforeach()

if(failures)
  list(JOIN failures "\n  " report)
  message(FATAL_ERROR "AlSimdAsmCheck: ${listing}\n  ${report}")
endif()
list(LENGTH seen seen_count)
message(STATUS "AlSimdAsmCheck: ${seen_count} operations within their ceilings in ${listing}")
