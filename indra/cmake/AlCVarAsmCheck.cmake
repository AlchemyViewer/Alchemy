# -*- cmake -*-
#
# Reads the assembly the compiler wrote for alcvar/tests/alcvar_asm_probe.cpp
# and fails when a settings handle's read of a scalar is anything but one
# load from its slot's own address and a return: no load of a pointer first,
# no call, no guard. Run as a test of the optimised configurations:
#
#   cmake -DOBJECT=<the probe's object file> -DSYNTAX=gnu|masm
#         -DARCH=x86|arm64 -P AlCVarAsmCheck.cmake
#
# The listing sits beside the object, as AlSimdAsmCheck.cmake finds its own.

cmake_minimum_required(VERSION 4.0)

foreach(var OBJECT SYNTAX ARCH)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "AlCVarAsmCheck: ${var} is required")
  endif()
endforeach()

get_filename_component(listing_dir "${OBJECT}" DIRECTORY)
file(GLOB listings "${listing_dir}/alcvar_asm_probe*.s" "${listing_dir}/alcvar_asm_probe*.asm")
list(LENGTH listings listing_count)
if(NOT listing_count EQUAL 1)
  message(FATAL_ERROR "AlCVarAsmCheck: expected one listing beside ${OBJECT}, found: ${listings}")
endif()
list(GET listings 0 listing)

if(SYNTAX STREQUAL "gnu")
  set(function_start "^_?(probe_read_[a-z0-9_]+):")
  set(function_end "^\t(ret|retq)([ \t]|$)")
  set(instruction "^\t[a-z]")
  set(directive "^\t\\.")
elseif(SYNTAX STREQUAL "masm")
  set(function_start "^\\|?(probe_read_[a-z0-9_]+)\\|?[ \t]+PROC")
  set(function_end "(^|[ \t])ENDP([ \t]|$)")
  set(instruction "^\t[a-z]")
  set(directive "^\tnpad")
else()
  message(FATAL_ERROR "AlCVarAsmCheck: SYNTAX is gnu or masm, not ${SYNTAX}")
endif()

# Matched against an instruction with its leading tab taken off: a branch,
# which a read that was not inlined is, and a load.
if(ARCH STREQUAL "x86")
  set(landing_pad "^\tendbr(32|64)([ \t]|$)")
  set(return "^\t(ret|retq)([ \t]|$)")
  set(branch "^(call|jmp|j[a-z]+)[a-z]*([ \t]|$)")
elseif(ARCH STREQUAL "arm64")
  set(landing_pad "^\t(bti([ \t]|$)|hint[ \t]+(34|36|38)([ \t]|$))")
  set(return "^\tret([ \t]|$)")
  set(branch "^(b|bl|br|blr|b\\.[a-z]+|cbn?z|tbn?z)([ \t]|$)")
  set(load "^ld[a-z0-9]*([ \t]|$)")
else()
  message(FATAL_ERROR "AlCVarAsmCheck: ARCH is x86 or arm64, not ${ARCH}")
endif()

# The probes the file defines, each a read of one scalar type.
set(
  expected
  f32
  bool
  s32
  u32
  f64
  s64
)

file(STRINGS "${listing}" lines)
set(current "")
set(body "")
set(failures "")
set(seen "")

function(_al_finish name body)
  if(NOT name)
    return()
  endif()
  list(LENGTH body count)
  if(ARCH STREQUAL "arm64")
    # The slot's address in two instructions, adrp and its offset, then the
    # load: still no load of a pointer.
    set(most 3)
  else()
    set(most 1)
  endif()
  # A read not inlined is one branch, and one through the global offset
  # table an address loaded and the load: each as short as the read, and
  # each naming the group's storage.
  set(branches 0)
  set(loads 0)
  set(got FALSE)
  foreach(instruction IN LISTS body)
    if(instruction MATCHES "${branch}")
      math(EXPR branches "${branches} + 1")
    endif()
    if(load AND instruction MATCHES "${load}")
      math(EXPR loads "${loads} + 1")
    endif()
    if(instruction MATCHES "(:got|@GOT)")
      set(got TRUE)
    endif()
  endforeach()
  string(JOIN "\n    " shown ${body})
  if(count EQUAL 0 OR count GREATER most)
    list(
      APPEND failures
      "probe_read_${name}: ${count} instructions where one load was wanted:\n    ${shown}"
    )
  elseif(branches GREATER 0)
    list(APPEND failures "probe_read_${name}: a branch, not the read inlined:\n    ${shown}")
  elseif(got)
    list(
      APPEND failures
      "probe_read_${name}: the slot's address loaded from the global offset table:\n    ${shown}"
    )
  elseif(load AND NOT loads EQUAL 1)
    list(APPEND failures "probe_read_${name}: ${loads} loads where one was wanted:\n    ${shown}")
  elseif(NOT body MATCHES "Group_")
    list(
      APPEND failures
      "probe_read_${name}: does not read its slot by the slot's own name:\n    ${shown}"
    )
  endif()
  set(failures "${failures}" PARENT_SCOPE)
endfunction()

foreach(line IN LISTS lines)
  if(line MATCHES "${function_start}")
    _al_finish("${current}" "${body}")
    string(REGEX REPLACE "^probe_read_" "" current "${CMAKE_MATCH_1}")
    list(APPEND seen ${current})
    set(body "")
    continue()
  endif()
  if(NOT current)
    continue()
  endif()
  if(line MATCHES "${function_end}")
    _al_finish("${current}" "${body}")
    set(current "")
    set(body "")
    continue()
  endif()
  if(
    line MATCHES "${instruction}"
    AND NOT line MATCHES "${directive}"
    AND NOT line MATCHES "${return}"
    AND NOT line MATCHES "${landing_pad}"
  )
    string(STRIP "${line}" stripped)
    string(REPLACE ";" "," stripped "${stripped}")
    list(APPEND body "${stripped}")
  endif()
endforeach()
_al_finish("${current}" "${body}")

foreach(name IN LISTS expected)
  if(NOT name IN_LIST seen)
    list(APPEND failures "probe_read_${name}: not found in ${listing}")
  endif()
endforeach()

if(failures)
  string(JOIN "\n  " report ${failures})
  message(FATAL_ERROR "AlCVarAsmCheck: a handle's read is not one load:\n  ${report}")
endif()
list(LENGTH seen probes)
message(STATUS "AlCVarAsmCheck: ${probes} handle reads, each one load from its slot")
