# -*- cmake -*-
#
# The script analyzers: Luau's front end from Second Life's fork for SLua,
# Tailslide for LSL. Language-neutral and free of UI; what the studio's
# language services are built on.

add_library(alscript STATIC)

# Each folder is a part of the library, and each is on the include path,
# so that a header is named alone wherever it is used:
#   core/          what both languages share: problems, spans and symbols, the
#                  lexicon, the messages, a script's weight, the formatter;
#   lint/          the studio's own lints over both languages, and the fixes;
#   lsl/           the LSL analyzer over Tailslide, and what reads LSL;
#   lsl/optimizer/ the LSL optimizer, a pass a file;
#   luau/          the SLua analyzer over Second Life's fork of Luau;
#   masters/       files on disk as the masters of scripts in the world:
#                  the links and the file they are kept in, finding a
#                  master, what a send does, the changes to them gathered
#                  over a burst, and the sends underway;
#   preprocessor/  the preprocessor, its includes and what it keeps.
target_sources(
  alscript
  PRIVATE
    core/almessagemap.cpp
    core/alnotecardformat.cpp
    core/alscriptformatter.cpp
    core/alscriptlexicon.cpp
    core/alscriptmessages.cpp
    core/alscriptnameindex.cpp
    core/alscriptoutlinepairs.cpp
    core/alscriptstack.cpp
    core/alscripttempfiles.cpp
    core/alscriptweight.cpp
    lint/allsllintpass.cpp
    lint/alscriptfixes.cpp
    lint/alscriptlintpass.cpp
    lint/alselenefilters.cpp
    lsl/allslconsts.cpp
    lsl/allsleffects.cpp
    lsl/allslexports.cpp
    lsl/allslservice.cpp
    lsl/allsltoslua.cpp
    lsl/allsltraits.cpp
    lsl/allslvalues.cpp
    lsl/optimizer/allslconstantglobals.cpp
    lsl/optimizer/allslcosts.cpp
    lsl/optimizer/allslbranches.cpp
    lsl/optimizer/allsldeadcode.cpp
    lsl/optimizer/allslflowvalues.cpp
    lsl/optimizer/allslfolder.cpp
    lsl/optimizer/allslglobals.cpp
    lsl/optimizer/allslinliner.cpp
    lsl/optimizer/allsllibraryfold.cpp
    lsl/optimizer/allslnames.cpp
    lsl/optimizer/allsloptimizer.cpp
    lsl/optimizer/allsloptimizerpass.cpp
    lsl/optimizer/allslprinter.cpp
    lsl/optimizer/allslrepeatedcalls.cpp
    lsl/optimizer/allslshapes.cpp
    lsl/optimizer/allslsignatures.cpp
    lsl/optimizer/allslsimplifier.cpp
    luau/alluaucompletion.cpp
    luau/alluauconfig.cpp
    luau/alluauconfigscript.cpp
    luau/alluauexports.cpp
    luau/alluaufragment.cpp
    luau/alluaufrontend.cpp
    luau/alluaunavigation.cpp
    luau/alluauservice.cpp
    luau/alluausharedstart.cpp
    luau/alluautaskpool.cpp
    luau/alluautypes.cpp
    masters/almasterburst.cpp
    masters/almasterindex.cpp
    masters/almasterlinks.cpp
    masters/almastermatch.cpp
    masters/almasterplan.cpp
    masters/almasterqueue.cpp
    preprocessor/aldiskcache.cpp
    preprocessor/aldiskincludes.cpp
    preprocessor/alincludeidentity.cpp
    preprocessor/alincludesearch.cpp
    preprocessor/allinelabel.cpp
    preprocessor/almodulelook.cpp
    preprocessor/alpreprocessor.cpp
    preprocessor/alrequirenavigation.cpp
    preprocessor/alscriptenvelope.cpp
    preprocessor/alscriptsnapshot.cpp
    preprocessor/alscripttextcache.cpp
    preprocessor/alsourcemap.cpp
    preprocessor/aluploadheader.cpp
)

target_sources(
  alscript
  PUBLIC
    FILE_SET HEADERS
      FILES
        core/almessagemap.h
        core/alnotecardformat.h
        core/alscriptengine.h
        core/alscriptformatter.h
        core/alscriptlexicon.h
        core/alscriptmessages.h
        core/alscriptnameindex.h
        core/alscriptoutlinepairs.h
        core/alscriptproblem.h
        core/alscriptspan.h
        core/alscriptstack.h
        core/alscriptsymbol.h
        core/alscripttempfiles.h
        core/alscriptweight.h
        lint/allsllintpass.h
        lint/alscriptfixes.h
        lint/alscriptlintpass.h
        lint/alselenefilters.h
        lsl/allslconsts.h
        lsl/allsleffects.h
        lsl/allsleventtext.inc
        lsl/allslexports.h
        lsl/allslitemargs.inc
        lsl/allslservice.h
        lsl/allsltoslua.h
        lsl/allsltraits.h
        lsl/allsltraits.inc
        lsl/allsluuids.inc
        lsl/allslvalues.h
        lsl/optimizer/allslconstantglobals.h
        lsl/optimizer/allslcosts.h
        lsl/optimizer/allslbranches.h
        lsl/optimizer/allsldeadcode.h
        lsl/optimizer/allslflowvalues.h
        lsl/optimizer/allslfolder.h
        lsl/optimizer/allslglobals.h
        lsl/optimizer/allslinliner.h
        lsl/optimizer/allsllibraryfold.h
        lsl/optimizer/allslnames.h
        lsl/optimizer/allsloptimizer.h
        lsl/optimizer/allsloptimizerpass.h
        lsl/optimizer/allslprinter.h
        lsl/optimizer/allslrepeatedcalls.h
        lsl/optimizer/allslshapes.h
        lsl/optimizer/allslsignatures.h
        lsl/optimizer/allslsimplifier.h
        luau/alluaucompletion.h
        luau/alluauconfig.h
        luau/alluauconfigscript.h
        luau/alluauexports.h
        luau/alluaufragment.h
        luau/alluaufrontend.h
        luau/alluaunavigation.h
        luau/alluauservice.h
        luau/alluausharedstart.h
        luau/alluautaskpool.h
        luau/alluautypes.h
        masters/almasterburst.h
        masters/almasterindex.h
        masters/almasterlinks.h
        masters/almastermatch.h
        masters/almasterplan.h
        masters/almasterqueue.h
        preprocessor/aldiskcache.h
        preprocessor/aldiskincludes.h
        preprocessor/alincludeidentity.h
        preprocessor/alincludesearch.h
        preprocessor/allinelabel.h
        preprocessor/almodulelook.h
        preprocessor/alpreprocessor.h
        preprocessor/alrequirenavigation.h
        preprocessor/alscriptenvelope.h
        preprocessor/alscriptjoblane.h
        preprocessor/alscriptsnapshot.h
        preprocessor/alscripttextcache.h
        preprocessor/alsourcemap.h
        preprocessor/aluploadheader.h
)

target_include_directories(
  alscript
  PUBLIC core lint lsl lsl/optimizer luau masters preprocessor
)

target_link_libraries(alscript PRIVATE al::flags ll::slua ll::tailslide llmath PUBLIC llcommon)
target_precompile_headers(alscript REUSE_FROM llprecompiled)

# The optimizer folds LSL's floats to what the VM gives and prints them so
# that they read back as themselves: IEEE's arithmetic a step at a time, and
# a zero's sign kept. The viewer's float flags (00-Common.cmake) let the
# compiler reassociate, fuse a product into a sum, take a reciprocal for a
# division and forget a zero's sign -- a vector normalised as a product with
# 1 / mag where the code divides -- so these are built without them, and
# without the precompiled header those flags were part of.
if(WINDOWS)
  set(alscript_ieee_options /fp:precise)
else()
  set(
    alscript_ieee_options
    -ffp-contract=off
    -fno-associative-math
    -fno-reciprocal-math
    -fsigned-zeros
  )
endif()
set_source_files_properties(
  lsl/allslvalues.cpp
  lsl/optimizer/allslbranches.cpp
  lsl/optimizer/allslconstantglobals.cpp
  lsl/optimizer/allsldeadcode.cpp
  lsl/optimizer/allslflowvalues.cpp
  lsl/optimizer/allslfolder.cpp
  lsl/optimizer/allsllibraryfold.cpp
  lsl/optimizer/allslnames.cpp
  lsl/optimizer/allsloptimizer.cpp
  lsl/optimizer/allsloptimizerpass.cpp
  lsl/optimizer/allslprinter.cpp
  lsl/optimizer/allslrepeatedcalls.cpp
  lsl/optimizer/allslshapes.cpp
  lsl/optimizer/allslsimplifier.cpp
  PROPERTIES COMPILE_OPTIONS "${alscript_ieee_options}" SKIP_PRECOMPILE_HEADERS TRUE
)

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
  # Completion and signature help over a fragment, against the whole
  # script, at the places a scripter asks, over every SLua script to hand;
  # the places found by Luau's own lexer.
  al_add_test(
    alluaufragment
    PROJECT alscript
    LIBRARIES alscript llcommon ll::slua
    DEFINES
      ${definitions_defines}
      ${templates_define}
      "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
  # Where a name is declared and used, across a script and the modules it
  # requires.
  al_add_test(
    alluaunavigation
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
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
  # The studio's own lints, each over real checks, with its fix made.
  al_add_test(
    alscriptlintpass
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  # The SLua checks, and LSL written again as SLua, again with Luau's new
  # type solver, which a scripter may choose (ALScriptLuauSolver): the same
  # tests, told by the environment.
  foreach(
    test
    alluauservice
    alluaufragment
    alluaunavigation
    alscriptfixes
    alscriptlintpass
    allsltoslua
  )
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
  al_add_test(allinelabel PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almodulelook PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alsourcemap PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptjoblane PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptnameindex PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptoutlinepairs PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptsnapshot PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscripttextcache PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptstack PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptenvelope PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(aluploadheader PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almessagemap PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alluauconfig PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptformatter PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptlexicon PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptmessages PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alnotecardformat PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscripttempfiles PROJECT alscript LIBRARIES alscript llcommon)
  # Disk masters: the changes gathered over a burst, the links and their
  # index, the index kept in a file, a master found from what a script says
  # or its name, what a send does, and the sends underway.
  al_add_test(almasterburst PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almasterindex PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almasterlinks PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almastermatch PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almasterplan PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almasterqueue PROJECT alscript LIBRARIES alscript llcommon)
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
  # grid's definitions; and Tailslide's own test scripts under
  # tests/tailslide/, for their breadth. The comments a toggle makes, as
  # Luau's own lexer reads them.
  al_add_test(
    allsltoslua
    PROJECT alscript
    LIBRARIES alscript llcommon ll::slua
    DEFINES ${definitions_defines} "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
  # The golden files under tests/preprocessor/: every test_*.lsl beside
  # its expected output.
  al_add_test(
    alpreprocessor
    PROJECT alscript
    LIBRARIES alscript llcommon ll::slua
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
