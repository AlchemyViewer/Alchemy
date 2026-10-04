# -*- cmake -*-
#
# LL's LSL compiler and LSO VM, restored from before ca08bd5aba removed
# them: what emits LSO bytecode and CIL as LL's own does, and what runs the
# LSO, a reference for the script tests and tools, never linked into the
# viewer. The VM's library calls are answered by stand-ins a test gives. The parts that follow from the
# grid's definitions -- the lexer's constants and events, the grammar's
# events, the newer event nodes and the library table -- are written by
# LL's own generator from lsl-definitions, as LL's build writes them, then
# put through bison and flex.

find_package(unofficial-lsl-definitions CONFIG REQUIRED)
find_package(BISON)
find_package(FLEX)

# What it needs that a test build may not have; without it the library is
# left out and the configuration report says why.
set(missing "")
if(NOT BISON_FOUND)
  list(APPEND missing "bison")
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
# LL's compiler over a handful of scripts, each to LSO and to CIL.
al_add_test(allslreference PROJECT lscript LIBRARIES lscript llcommon)
# Tailslide's LSO images and CIL, and the Mono weigher, held against LL's
# compiler's over Tailslide's test scripts.
al_add_test(
  allslreferencecheck
  PROJECT lscript
  LIBRARIES lscript alscript llcommon
  DEFINES
    "AL_LSL_DEFINITIONS_DIR=\"${LSL_DEFINITIONS_DIR}\""
    "AL_ALSCRIPT_TEST_DIR=\"${INDRA_SOURCE_DIR}/alscript/tests\""
)


