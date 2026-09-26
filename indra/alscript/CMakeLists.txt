# -*- cmake -*-
#
# The script analyzers: Luau's front end from Second Life's fork for SLua,
# Tailslide for LSL. Language-neutral and free of UI; what the studio's
# language services are built on.

add_library(alscript STATIC)

target_sources(
  alscript
  PRIVATE
    aldiskincludes.cpp
    allslinliner.cpp
    almessagemap.cpp
    allsloptimizer.cpp
    alluauconfig.cpp
    alluauexports.cpp
    alluauservice.cpp
    allslexports.cpp
    allslservice.cpp
    allsltraits.cpp
    alpreprocessor.cpp
    alscriptenvelope.cpp
    alscriptfixes.cpp
    alscriptformatter.cpp
    alscriptmessages.cpp
    alscriptrecovery.cpp
    alscriptweight.cpp
    alscriptstack.cpp
    alsourcemap.cpp
)

target_sources(
  alscript
  PUBLIC
    FILE_SET HEADERS
      FILES
        aldiskincludes.h
        allslinliner.h
        almessagemap.h
        allsloptimizer.h
        allsltraits.inc
        alluauconfig.h
        alluauexports.h
        alluauservice.h
        allslexports.h
        allslservice.h
        allsltraits.h
        alpreprocessor.h
        alscriptengine.h
        alscriptenvelope.h
        alscriptfixes.h
        alscriptformatter.h
        alscriptmessages.h
        alscriptrecovery.h
        alscriptproblem.h
        alscriptstack.h
        alscriptsymbol.h
        alscriptweight.h
        alsourcemap.h
)

target_link_libraries(alscript PRIVATE al::flags ll::slua ll::tailslide llmath PUBLIC llcommon)
target_precompile_headers(alscript REUSE_FROM llprecompiled)

# tests
if(AL_BUILD_TESTS)
  # The tests check real scripts against the grid's own definitions, which
  # the lsl-definitions port ships.
  find_package(unofficial-lsl-definitions CONFIG REQUIRED)
  set(definitions_defines "AL_LSL_DEFINITIONS_DIR=\"${LSL_DEFINITIONS_DIR}\"")
  al_add_test(
    alluauservice
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  al_add_test(
    allslservice
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  # Each fix made over a real check, applied, and the text checked again.
  al_add_test(
    alscriptfixes
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  # The SLua checks again with Luau's new type solver, which a scripter may
  # choose (ALScriptLuauSolver): the same tests, told by the environment.
  foreach(test alluauservice alscriptfixes)
    add_test(
      NAME INTEGRATION_TEST_RUNNER_${test}_newsolver
      COMMAND $<TARGET_FILE:INTEGRATION_TEST_${test}>
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(
      INTEGRATION_TEST_RUNNER_${test}_newsolver
      PROPERTIES
        ENVIRONMENT "AL_TEST_LUAU_SOLVER=new"
        ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
        SKIP_RETURN_CODE 125
    )
  endforeach()
  # What a script weighs: SLua by its bytecode, LSO by Tailslide's image,
  # with the grid's builtins loaded.
  al_add_test(
    alscriptweight
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  al_add_test(aldiskincludes PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptstack PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptenvelope PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almessagemap PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alluauconfig PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptformatter PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptmessages PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptrecovery PROJECT alscript LIBRARIES alscript llcommon)
  # The optimizer's golden files under tests/optimizer/, checked with the
  # grid's builtins loaded.
  al_add_test(
    allsloptimizer
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
  # The golden files under tests/preprocessor/: every test_*.lsl beside
  # its expected output.
  al_add_test(
    alpreprocessor
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
  # The preprocessor, the formatter, the optimizer and one analysis of each
  # language over big scripts, with the grid's definitions loaded.
  al_add_bench(
    alscript
    PROJECT alscript
    LIBRARIES alscript llcommon
    INCLUDES ${INDRA_SOURCE_DIR}/test
    DEFINES ${definitions_defines}
  )
endif()
