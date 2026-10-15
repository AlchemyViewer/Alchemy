# -*- cmake -*-
#
# LL's LSL compiler and LSO VM, restored from before ca08bd5aba removed
# them: what emits LSO bytecode and CIL as LL's own does, and what runs the
# LSO, a reference for the script tests and tools, never linked into the
# viewer. The VM's library calls are answered by the test library
# (tests/allsolibrary.h). The parts that follow from the grid's definitions -- the lexer's constants and events, the grammar's
# events, the newer event nodes and the library table -- are written by
# LL's own generator from lsl-definitions, as LL's build writes them, then
# put through bison and flex.

find_package(unofficial-lsl-definitions CONFIG REQUIRED)
# Bison 3: the grammar's %destructor names a type, which bison 2.3, macOS's
# own, cannot read. On macOS Homebrew's bison and flex are looked for first,
# which it keeps off the path (brew install bison flex).
if(APPLE)
  foreach(homebrew IN ITEMS /opt/homebrew/opt /usr/local/opt)
    list(APPEND CMAKE_PROGRAM_PATH ${homebrew}/bison/bin ${homebrew}/flex/bin)
  endforeach()
endif()
find_package(BISON 3.0)
find_package(FLEX)

# What it needs that a test build may not have; without it the library is
# left out and the configuration report says why.
set(missing "")
if(NOT BISON_FOUND)
  list(APPEND missing "bison 3")
endif()
if(NOT FLEX_FOUND)
  list(APPEND missing "flex")
endif()
if(NOT LSL_DEFINITIONS_GENERATOR OR NOT EXISTS "${LSL_DEFINITIONS_GENERATOR}")
  list(APPEND missing "lsl-definitions' generator")
endif()
if(NOT Python3_Interpreter_FOUND)
  list(APPEND missing "Python")
else()
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import yaml, llsd"
    RESULT_VARIABLE python_modules
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(NOT python_modules EQUAL 0)
    list(APPEND missing "Python's yaml and llsd modules")
  endif()
endif()
if(missing)
  list(JOIN missing ", " missing)
  set_property(GLOBAL PROPERTY AL_LSCRIPT_STATUS "not built; it needs ${missing}")
  return()
endif()
set_property(GLOBAL PROPERTY AL_LSCRIPT_STATUS "built, for the tests")

set(generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(definitions ${LSL_DEFINITIONS_DIR}/lsl_definitions.yaml)
file(MAKE_DIRECTORY ${generated})

# One of LL's generator's commands, from the definitions into generated/.
function(lscript_generate output command)
  add_custom_command(
    OUTPUT ${generated}/${output}
    COMMAND
      ${Python3_EXECUTABLE} ${LSL_DEFINITIONS_GENERATOR} ${definitions} ${command} ${ARGN}
      ${generated}/${output}
    DEPENDS ${LSL_DEFINITIONS_GENERATOR} ${definitions} ${ARGN}
    COMMENT "Generating ${output} from lsl-definitions"
    VERBATIM
  )
endfunction()

lscript_generate(indra.l gen_lexer_file ${CMAKE_CURRENT_SOURCE_DIR}/lscript_compile/indra.l)
lscript_generate(indra.y gen_parser_file ${CMAKE_CURRENT_SOURCE_DIR}/lscript_compile/indra.y)
lscript_generate(lscript_tree_events.h gen_tree_header_file)
lscript_generate(lscript_tree_events.inc gen_tree_source_file)
lscript_generate(lscript_library_defs.inc gen_lscript_library_defs)

bison_target(
  indra_parser
  ${generated}/indra.y
  ${generated}/indra.y.cpp
  DEFINES_FILE ${generated}/indra.y.hpp
  COMPILE_FLAGS "-p indra_"
)
flex_target(indra_lexer ${generated}/indra.l ${generated}/indra.l.cpp COMPILE_FLAGS "-P indra_")
add_flex_bison_dependency(indra_lexer indra_parser)

add_library(lscript STATIC)

set(
  lscript_sources
  allslreference.cpp
  lscript_compile/lscript_bytecode.cpp
  lscript_compile/lscript_error.cpp
  lscript_compile/lscript_heap.cpp
  lscript_compile/lscript_resource.cpp
  lscript_compile/lscript_scope.cpp
  lscript_compile/lscript_tree.cpp
  lscript_compile/lscript_typecheck.cpp
  lscript_execute/llscriptresource.cpp
  lscript_execute/llscriptresourceconsumer.cpp
  lscript_execute/llscriptresourcepool.cpp
  lscript_execute/lscript_execute.cpp
  lscript_execute/lscript_heapruntime.cpp
  lscript_execute/lscript_readlso.cpp
  lscript_library/lscript_alloc.cpp
  lscript_library/lscript_library.cpp
  ${BISON_indra_parser_OUTPUT_SOURCE}
  ${FLEX_indra_lexer_OUTPUTS}
)
target_sources(
  lscript
  PRIVATE
    ${lscript_sources}
    ${generated}/lscript_tree_events.h
    ${generated}/lscript_tree_events.inc
    ${generated}/lscript_library_defs.inc
)

target_sources(
  lscript
  PUBLIC
    FILE_SET HEADERS
      FILES
        allslreference.h
        llscriptresource.h
        llscriptresourceconsumer.h
        llscriptresourcepool.h
        lscript_alloc.h
        lscript_byteconvert.h
        lscript_byteformat.h
        lscript_execute.h
        lscript_export.h
        lscript_http.h
        lscript_library.h
        lscript_rt_interface.h
        lscript_compile/lscript_bytecode.h
        lscript_compile/lscript_error.h
        lscript_compile/lscript_heap.h
        lscript_compile/lscript_resource.h
        lscript_compile/lscript_scope.h
        lscript_compile/lscript_tree.h
        lscript_compile/lscript_typecheck.h
        lscript_execute/lscript_heapruntime.h
        lscript_execute/lscript_readlso.h
)

target_include_directories(
  lscript
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lscript_compile
    ${CMAKE_CURRENT_SOURCE_DIR}/lscript_execute
    ${generated}
)
target_link_libraries(lscript PRIVATE al::flags PUBLIC llcommon llmath)

# LL's code as it was, which the tree's warnings were never asked of.
set_target_properties(lscript PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# The compiler reads and writes LSL's floats as single precision, a step at
# a time, as allslvalues.cpp does; the viewer's float flags would let the
# compiler here reassociate or fuse what LL's did not.
if(WINDOWS)
  set(lscript_ieee_options /fp:precise)
  set_source_files_properties(
    ${FLEX_indra_lexer_OUTPUTS}
    PROPERTIES COMPILE_DEFINITIONS YY_NO_UNISTD_H
  )
else()
  set(
    lscript_ieee_options
    -ffp-contract=off
    -fno-associative-math
    -fno-reciprocal-math
    -fsigned-zeros
  )
endif()
set_source_files_properties(${lscript_sources} PROPERTIES COMPILE_OPTIONS "${lscript_ieee_options}")

# tests
# The LSL library as the tests and the fuzzers run LL's VM with it.
add_library(lscript_test_library OBJECT tests/allsolibrary.cpp)
target_link_libraries(lscript_test_library PRIVATE al::flags lscript)
# It answers as SLua's builtins do, signed zeros and all, which the viewer's
# float flags would let the compiler drop: under -fno-signed-zeros Clang
# folds "value == 0 ? 0 : value" to value, and llDumpList2String gave
# -0.000000 on macOS.
set_source_files_properties(
  tests/allsolibrary.cpp
  PROPERTIES COMPILE_OPTIONS "${lscript_ieee_options}"
)
set_target_properties(lscript_test_library PROPERTIES FOLDER "Tests/lscript")

# LL's compiler over a handful of scripts, each to LSO and to CIL; and
# Tailslide's LSO images and CIL, and the Mono weigher, held against LL's
# compiler's over Tailslide's test scripts; and LL's VM running scripts. One
# Catch2 binary, each file run in a process of its own.
al_add_test_binary(
  lscript_tests
  PROJECT lscript
  SOURCES tests/allslreference_test.cpp tests/allslreferencecheck_test.cpp tests/allsovm_test.cpp
  SUPPORT alscript_test_checks llcommon_test_checks lscript_test_library
  LIBRARIES lscript alscript llcommon
  DEFINES
    "AL_LSL_DEFINITIONS_DIR=\"${LSL_DEFINITIONS_DIR}\""
    "AL_ALSCRIPT_TEST_DIR=\"${INDRA_SOURCE_DIR}/alscript/tests\""
    "AL_LSCRIPT_REGRESSION_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/regressions\""
)

# fuzzers
# libFuzzer on LL's compiler and VM, from what a script's author controls,
# its source: lscript_fuzz_source takes the bytes as LSL, and
# lscript_fuzz_program has them choose a well-typed script, which gets past
# the parser to the code generation and the VM. lscript is built with
# libFuzzer's coverage for them, whose hooks the sanitizer runtime answers
# in lscript_tests. Each has a short run as a test: the program fuzzer over
# inputs from a fixed seed, the source fuzzer over the scripts of alscript's
# tests and the regression scripts (tests/regressions), each a fault one of
# them found, which lscript_tests runs as well. doc/TESTING.md says how to
# run them for longer.
if(AL_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT "address" IN_LIST AL_SANITIZERS)
    message(FATAL_ERROR "AL_BUILD_FUZZERS needs Clang and address in AL_SANITIZERS")
  endif()
  target_compile_options(lscript PRIVATE -fsanitize=fuzzer-no-link)
  foreach(fuzzer IN ITEMS source program)
    add_executable(lscript_fuzz_${fuzzer} fuzz/allsofuzz${fuzzer}.cpp)
    target_compile_options(lscript_fuzz_${fuzzer} PRIVATE -fsanitize=fuzzer)
    target_link_options(lscript_fuzz_${fuzzer} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(
      lscript_fuzz_${fuzzer}
      PRIVATE al::flags lscript lscript_test_library llcommon llmath
    )
    set_target_properties(lscript_fuzz_${fuzzer} PROPERTIES FOLDER "Tests/lscript")
  endforeach()

  add_test(
    NAME lscript_fuzz_program
    COMMAND lscript_fuzz_program -runs=3000 -seed=1 -close_fd_mask=1
  )
  add_test(
    NAME lscript_fuzz_source
    COMMAND
      lscript_fuzz_source -runs=0 -close_fd_mask=1 -dict=${CMAKE_CURRENT_SOURCE_DIR}/fuzz/lsl.dict
      ${INDRA_SOURCE_DIR}/alscript/tests/tailslide/scripts
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/regressions
  )
  set_tests_properties(
    lscript_fuzz_program
    lscript_fuzz_source
    PROPERTIES
      LABELS "lscript;fuzz"
      ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
      FAIL_REGULAR_EXPRESSION "${AL_TEST_FAIL_REGULAR_EXPRESSION}"
  )
endif()
