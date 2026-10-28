# -*- cmake -*-
#
# AlCVarAsmCheck.cmake given listings of each kind it is to tell apart: a
# read inlined as one load passes; a read through the global offset table, a
# read not inlined, and a read of the wrong slot fail.
#
#   cmake -DINDRA_SOURCE_DIR=<indra> -DWORK_DIR=<scratch> -P alcvar_asm_check_test.cmake

cmake_minimum_required(VERSION 4.0)

set(
  probes
  f32
  bool
  s32
  u32
  f64
  s64
)
set(failures "")

# A listing of every probe, each read as `lines` say (; between lines),
# checked as SYNTAX and ARCH; `expected` PASS or FAIL.
function(
  check
  what
  syntax
  arch
  extension
  expected
  lines
)
  string(MAKE_C_IDENTIFIER "${what}" dir_name)
  set(dir "${WORK_DIR}/${dir_name}")
  file(REMOVE_RECURSE "${dir}")
  file(MAKE_DIRECTORY "${dir}")
  set(text "")
  foreach(probe IN LISTS probes)
    if(syntax STREQUAL "gnu")
      string(APPEND text "probe_read_${probe}:\n")
    else()
      string(APPEND text "probe_read_${probe} PROC\n")
    endif()
    foreach(line IN LISTS lines)
      string(APPEND text "\t${line}\n")
    endforeach()
    if(syntax STREQUAL "masm")
      string(APPEND text "probe_read_${probe} ENDP\n")
    endif()
  endforeach()
  file(WRITE "${dir}/alcvar_asm_probe.${extension}" "${text}")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} "-DOBJECT=${dir}/alcvar_asm_probe.o" -DSYNTAX=${syntax} -DARCH=${arch} -P
      "${INDRA_SOURCE_DIR}/cmake/AlCVarAsmCheck.cmake"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )
  if(result EQUAL 0)
    set(outcome PASS)
  else()
    set(outcome FAIL)
  endif()
  if(NOT outcome STREQUAL expected)
    set(failures "${failures}\n  ${what}: ${outcome}, not ${expected}\n${output}" PARENT_SCOPE)
  endif()
endfunction()

# x86, as GCC and Clang write it.
check("x86 one load" gnu x86 s PASS "movss\t_ZN7cvartest6render6Group_6wordsE+4(%rip), %xmm0;retq")
check("x86 through the GOT" gnu x86 s FAIL
      "movq\t_ZN7cvartest6render6Group_6wordsE@GOTPCREL(%rip), %rax;movss\t4(%rax), %xmm0;retq"
)
check("x86 not inlined" gnu x86 s FAIL "jmp\t_ZNK6ALCVarIN7cvartest6render6Group_ELj0EE4readEv")
# x86, as MSVC writes it.
check("x86 MSVC one load" masm x86 asm PASS
      "movss\txmm0, DWORD PTR ?words@Group_@render@cvartest@@2PAIA+4;ret\t0"
)
check("x86 MSVC not inlined" masm x86 asm FAIL "jmp\t?read@?$ALCVar@UGroup_@render@cvartest@@$0A@@@QEBAMXZ")
# arm64, as Clang and GCC write it: the slot's page and offset, then the load.
check("arm64 one load" gnu arm64 s PASS
      "adrp\tx8, _ZN7cvartest6render6Group_6wordsE;ldr\ts0, [x8, :lo12:_ZN7cvartest6render6Group_6wordsE+4];ret"
)
check("arm64 through the GOT" gnu arm64 s FAIL
      "adrp\tx8, :got:_ZN7cvartest6render6Group_6wordsE;ldr\tx8, [x8, :got_lo12:_ZN7cvartest6render6Group_6wordsE];ldr\ts0, [x8, #4];ret"
)
check("arm64 Mach-O through the GOT" gnu arm64 s FAIL
      "adrp\tx8, __ZN7cvartest6render6Group_6wordsE@GOTPAGE;ldr\tx8, [x8, __ZN7cvartest6render6Group_6wordsE@GOTPAGEOFF];ldr\ts0, [x8, #4];ret"
)
# arm64, as MSVC writes a read it did not inline.
check("arm64 MSVC not inlined" masm arm64 asm FAIL "b\t|?read@?$ALCVar@UGroup_@render@cvartest@@$0A@@@QEBAMXZ|")
check("arm64 two loads" gnu arm64 s FAIL
      "adrp\tx8, _ZN7cvartest6render6Group_6wordsE;ldr\tw9, [x8];ldr\ts0, [x8, #4];ret"
)
# Not the group's slot at all.
check("x86 another slot" gnu x86 s FAIL "movss\tsomething_else(%rip), %xmm0;retq")

if(failures)
  message(FATAL_ERROR "AlCVarAsmCheck tells the listings apart wrongly:${failures}")
endif()
message(STATUS "AlCVarAsmCheck tells every listing apart")
