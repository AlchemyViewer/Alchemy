# -*- cmake -*-
#
# The script analyzers: Luau's front end from Second Life's fork for SLua,
# Tailslide for LSL. Language-neutral and free of UI; what the studio's
# language services are built on.

add_library(alscript STATIC)

target_sources(
  alscript
  PRIVATE
    aldiskcache.cpp
    aldiskincludes.cpp
    alincludeidentity.cpp
    alincludesearch.cpp
    almodulelook.cpp
    allslconsts.cpp
    allslcosts.cpp
    allsleffects.cpp
    allsllintpass.cpp
    allslinliner.cpp
    almessagemap.cpp
    allsloptimizer.cpp
    alluauconfig.cpp
    alluauexports.cpp
    alluauservice.cpp
    allslexports.cpp
    allslservice.cpp
    allsltoslua.cpp
    allsltraits.cpp
    allslvalues.cpp
    alpreprocessor.cpp
    alscriptenvelope.cpp
    alscriptfixes.cpp
    alscriptformatter.cpp
    alscriptlexicon.cpp
    alscriptlintpass.cpp
    alscriptmessages.cpp
    alscriptnameindex.cpp
    alscriptsnapshot.cpp
    alnotecardformat.cpp
    alscriptweight.cpp
    alscriptstack.cpp
    alscripttempfiles.cpp
    alscripttextcache.cpp
    alselenefilters.cpp
    alsourcemap.cpp
)

target_sources(
  alscript
  PUBLIC
    FILE_SET HEADERS
      FILES
        aldiskcache.h
        aldiskincludes.h
        alincludeidentity.h
        alincludesearch.h
        almodulelook.h
        allslconsts.h
        allslcosts.h
        allsleffects.h
        allsllintpass.h
        allslinliner.h
        almessagemap.h
        allsloptimizer.h
        allsltraits.inc
        allsluuids.inc
        alluauconfig.h
        alluauexports.h
        alluauservice.h
        allslexports.h
        allslservice.h
        allsltoslua.h
        allsltraits.h
        allslvalues.h
        alpreprocessor.h
        alscriptengine.h
        alscriptenvelope.h
        alscriptfixes.h
        alscriptformatter.h
        alscriptjoblane.h
        alscriptlexicon.h
        alscriptlintpass.h
        alscriptmessages.h
        alscriptnameindex.h
        alscriptsnapshot.h
        alscriptspan.h
        alnotecardformat.h
        alscriptproblem.h
        alscriptstack.h
        alscriptsymbol.h
        alscripttempfiles.h
        alscripttextcache.h
        alscriptweight.h
        alselenefilters.h
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
  # The templates Script Studio offers for a new script, which must check.
  set(
    templates_define
    "AL_SCRIPT_TEMPLATES_DIR=\"${INDRA_SOURCE_DIR}/newview/app_settings/script_templates\""
  )
  al_add_test(
    alluauservice
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} ${templates_define}
  )
  al_add_test(
    allslservice
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} ${templates_define}
  )
  # Each fix made over a real check, applied, and the text checked again.
  al_add_test(
    alscriptfixes
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  # The SLua checks, and LSL written again as SLua, again with Luau's new
  # type solver, which a scripter may choose (ALScriptLuauSolver): the same
  # tests, told by the environment.
  foreach(test alluauservice alscriptfixes allsltoslua)
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
  al_add_test(aldiskcache PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alincludeidentity PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alincludesearch PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almodulelook PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alsourcemap PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptjoblane PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptnameindex PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptsnapshot PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscripttextcache PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptstack PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptenvelope PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almessagemap PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alluauconfig PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptformatter PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptlexicon PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptmessages PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alnotecardformat PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscripttempfiles PROJECT alscript LIBRARIES alscript llcommon)
  # The preprocessor against C11's 6.10, clause by clause.
  al_add_test(alpreprocessorc11 PROJECT alscript LIBRARIES alscript llcommon)
  # What LSL may change and the order it runs in, over scripts parsed with
  # the grid's builtins loaded.
  al_add_test(
    allsleffects
    PROJECT alscript
    LIBRARIES alscript llcommon ll::tailslide
    DEFINES ${definitions_defines}
  )
  # What LSL forms cost on each target: snippet pairs compiled by
  # Tailslide, against what the optimizer takes them to cost.
  al_add_test(
    allslcosts
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  # The const keyword over scripts parsed with the grid's builtins loaded.
  al_add_test(
    allslconsts
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  # The optimizer's golden files under tests/optimizer/, checked with the
  # grid's builtins loaded.
  al_add_test(
    allsloptimizer
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
  # LSL written again as SLua, each result checked as SLua against the
  # grid's definitions.
  al_add_test(
    allsltoslua
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
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
