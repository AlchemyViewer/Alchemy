# -*- cmake -*-
#
# Fails when a source outside the SIMD ops layer names an x86 or NEON
# intrinsic, or when any source names glm. Everything the viewer does with a
# vector register goes through llcommon/alsimd.h and the kernels in
# llmath/alsimdkernels.inl, so that a body written once runs natively on
# x86-64 and aarch64 alike; an intrinsic anywhere else is code that compiles
# on one architecture. The math vocabulary is llmath's own; glm was retired
# in favour of it and is not a dependency. Run as a test:
#
#   cmake -DSOURCE_DIR=<indra> -P AlIntrinsicCheck.cmake

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "AlIntrinsicCheck: SOURCE_DIR is required")
endif()

# The files that are the backend, and the tests and bench that compare
# against the raw instruction on purpose.
set(
  allowed
  llcommon/alsimd.h
  llcommon/tests/alsimd_test.cpp
  llcommon/tests/alsimd_asm_probe.cpp
  llmath/alsimdkernels.inl
  llmath/tests/alsimdkernels_test.cpp
  llmath/tests/llsimd_bench.cpp
)

# _mm_, _mm256_ and _mm512_ for x86; for NEON the ACLE names, a mnemonic
# with an optional q and a lane type suffix. The mnemonic list keeps an
# ordinary identifier such as value_u8 from matching.
set(x86_pattern "_mm(256|512)?_[a-z0-9_]+")
set(
  neon_mnemonics
  "ld[1-4]|st[1-4]|add|sub|mul|fma|fms|mla|mls|div|min|max|abs|neg|dup|get|set|ext|zip|uzp|trn|rev|cvt|rnd|sqrt|rsqrt|recp|ceq|cgt|cge|clt|cle|and|orr|eor|bic|bsl|mvn|tbl|tbx|mov|padd|addv|maxv|minv|reinterpret|combine|shl|shr|tst|abd|pmax|pmin|cnt|clz|copy|create"
)
set(neon_pattern "v(${neon_mnemonics})[a-z0-9]*q?_[fsup](8|16|32|64)(x[0-9])?")
set(glm_pattern "glm::|glm/|GLM_")

file(
  GLOB_RECURSE sources
  RELATIVE "${SOURCE_DIR}"
  "${SOURCE_DIR}/*.h"
  "${SOURCE_DIR}/*.hpp"
  "${SOURCE_DIR}/*.inl"
  "${SOURCE_DIR}/*.cpp"
  "${SOURCE_DIR}/*.c"
)

set(offences)
foreach(source IN LISTS sources)
  if(source MATCHES "^externals/" OR source IN_LIST allowed)
    continue()
  endif()
  file(
    STRINGS "${SOURCE_DIR}/${source}"
    lines
    REGEX "${x86_pattern}|${neon_pattern}|${glm_pattern}"
  )
  foreach(line IN LISTS lines)
    if(
      line MATCHES "(^|[^A-Za-z0-9_])(${x86_pattern}|${neon_pattern})([^A-Za-z0-9_]|$)"
      OR line MATCHES "${glm_pattern}"
    )
      string(STRIP "${line}" line)
      string(REPLACE ";" "\;" line "${line}")
      list(APPEND offences "${source}: ${line}")
    endif()
  endforeach()
endforeach()

list(LENGTH offences offence_count)
if(offence_count GREATER 0)
  list(JOIN offences "\n  " report)
  message(
    FATAL_ERROR
    "AlIntrinsicCheck: ${offence_count} line(s) name an intrinsic outside the ops layer or glm; write them on alsimd:: and the llmath types instead:\n  ${report}"
  )
endif()
message(STATUS "AlIntrinsicCheck: no intrinsic outside the ops layer, no glm")
