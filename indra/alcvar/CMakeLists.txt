# -*- cmake -*-

# The settings store: each setting's value in a typed slot that any thread
# reads with one load, the layers the value is worked out from, and the
# schema alcvargen compiles from indra/cvars, whose handles every library
# above reads.

set(
  alcvar_SOURCES
  alcvarconvert.cpp
  alcvarfile.cpp
  alcvarpreview.cpp
  alcvarref.cpp
  alcvars.cpp
  alcvarvalue.cpp
  alcvarxmlimport.cpp
)

set(
  alcvar_HEADERS
  alcvar.h
  alcvaratomic.h
  alcvarconnection.h
  alcvarconvert.h
  alcvarconvertible.h
  alcvardesc.h
  alcvarfile.h
  alcvarpreview.h
  alcvarref.h
  alcvars.h
  alcvartype.h
  alcvarvalue.h
  alcvarxmlimport.h
)

add_library(alcvar STATIC)
target_sources(alcvar PRIVATE ${alcvar_SOURCES} PUBLIC FILE_SET HEADERS FILES ${alcvar_HEADERS})
target_link_libraries(alcvar PRIVATE al::flags PUBLIC llcommon llmath aldb)
target_precompile_headers(alcvar REUSE_FROM llprecompiled)

include(ALCVarGen)

# The viewer's schema, one file an area.
file(GLOB cvars_schema CONFIGURE_DEPENDS "${INDRA_SOURCE_DIR}/cvars/*.toml")
al_generate_cvars(
  alcvar
  ROOT cvar
  INCLUDE_DIR cvars
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
  LOCK "${INDRA_SOURCE_DIR}/cvars/cvars.lock"
  LOCK_TARGET cvars_lock
  SCHEMA ${cvars_schema}
)

if(AL_BUILD_TESTS)
  # The tests' own schema, under cvartest::, so that they run against
  # settings of their own rather than the viewer's.
  add_library(alcvar_test_schema OBJECT)
  target_link_libraries(alcvar_test_schema PRIVATE al::flags PUBLIC alcvar)
  set_target_properties(alcvar_test_schema PROPERTIES FOLDER "Tests/alcvar")
  al_generate_cvars(
    alcvar_test_schema
    ROOT cvartest
    INCLUDE_DIR cvartest
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_schema"
    LOCK "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/cvars.lock"
    SCHEMA
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/render.toml
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/ui.toml
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/chat.toml
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/perf.toml
  )

  al_add_test_binary(
    alcvar_tests
    PROJECT alcvar
    SOURCES
      tests/alcvarschema_test.cpp
      tests/alcvarlayers_test.cpp
      tests/alcvarlisteners_test.cpp
      tests/alcvarthreads_test.cpp
      tests/alcvarconvert_test.cpp
      tests/alcvarfile_test.cpp
      tests/alcvarxmlimport_test.cpp
      tests/alcvarpreview_test.cpp
    LIBRARIES alcvar
    SUPPORT alcvar_test_schema aldb_test_support llcommon_test_checks
    INCLUDES ${INDRA_SOURCE_DIR}/aldb/tests
    BENCH_SOURCES tests/alcvar_bench.cpp
  )

  # alcvar_preview <settings.db>: what an open would do to each row of a
  # settings file, writing nothing. Built with the tests' schema until the
  # viewer's declares settings.
  add_executable(alcvar_preview tools/alcvar_preview.cpp)
  target_link_libraries(alcvar_preview PRIVATE al::flags alcvar alcvar_test_schema)
  set_target_properties(alcvar_preview PROPERTIES FOLDER "Tools")

  # A handle's read of each scalar type, compiled with the tree's flags and
  # read by AlCVarAsmCheck.cmake: one load from the slot's own address and a
  # return, in the optimised configurations. It takes the schema's headers
  # alone, linking nothing, and never under link-time optimisation, which
  # leaves the compiler nothing to list. A sanitized tree's listing is the
  # sanitizers' as much as the handle's.
  if(NOT AL_SANITIZING)
    add_library(alcvar_asm_probe OBJECT tests/alcvar_asm_probe.cpp)
    target_link_libraries(alcvar_asm_probe PRIVATE al::flags $<COMPILE_ONLY:alcvar_test_schema>)
    set_target_properties(
      alcvar_asm_probe
      PROPERTIES
        FOLDER "Tests/alcvar"
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
        INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    )
    if(COMPILER_IS_MSVC)
      target_compile_options(
        alcvar_asm_probe
        PRIVATE /FAs "/Fa$<PATH:GET_PARENT_PATH,$<TARGET_OBJECTS:alcvar_asm_probe>>/"
      )
      set(alcvar_asm_syntax masm)
    elseif(COMPILER_IS_CLANG_CL)
      target_compile_options(alcvar_asm_probe PRIVATE /clang:-save-temps=obj)
      set(alcvar_asm_syntax gnu)
    else()
      target_compile_options(alcvar_asm_probe PRIVATE -save-temps=obj)
      set(alcvar_asm_syntax gnu)
    endif()
    if(BUILD_TARGET_IS_ARM64)
      set(alcvar_asm_arch arm64)
    else()
      set(alcvar_asm_arch x86)
    endif()
    add_test(
      NAME alcvar_asm_probe
      CONFIGURATIONS RelWithDebInfo Release
      COMMAND
        ${CMAKE_COMMAND} -DOBJECT=$<TARGET_OBJECTS:alcvar_asm_probe> -DSYNTAX=${alcvar_asm_syntax}
        -DARCH=${alcvar_asm_arch} -P ${INDRA_SOURCE_DIR}/cmake/AlCVarAsmCheck.cmake
    )
    set_tests_properties(alcvar_asm_probe PROPERTIES LABELS alcvar)
    add_dependencies(BUILD_TESTS alcvar_asm_probe)
  endif()
  # The check itself, given listings it is to pass and to fail.
  add_test(
    NAME alcvar_asm_check
    COMMAND
      ${CMAKE_COMMAND} -DINDRA_SOURCE_DIR=${INDRA_SOURCE_DIR}
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/asm_check_test -P
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/alcvar_asm_check_test.cmake
  )
  set_tests_properties(alcvar_asm_check PROPERTIES LABELS alcvar)
endif()
