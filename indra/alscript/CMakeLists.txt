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
#                  master, what a send does, the files watched and their
#                  changes gathered over a burst, and the sends underway;
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
    lsl/allsldepth.cpp
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
    luau/alluaushadowed.cpp
    luau/alluausharedstart.cpp
    luau/alluautypes.cpp
    masters/almasterburst.cpp
    masters/almasterclock.cpp
    masters/almasterindex.cpp
    masters/almasterlinks.cpp
    masters/almastermatch.cpp
    masters/almasterplan.cpp
    masters/almasterqueue.cpp
    masters/almasterwatch.cpp
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
        lsl/allsldepth.h
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
        luau/alluaushadowed.h
        luau/alluausharedstart.h
        luau/alluautypes.h
        masters/almasterburst.h
        masters/almasterclock.h
        masters/almasterindex.h
        masters/almasterlinks.h
        masters/almastermatch.h
        masters/almasterplan.h
        masters/almasterqueue.h
        masters/almasterwatch.h
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
# Not with the fuzzers, whose coverage flags the shared header was not
# built with, which Clang refuses.
if(NOT AL_BUILD_FUZZERS)
  target_precompile_headers(alscript REUSE_FROM llprecompiled)
endif()

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
  # What every binary of alscript's tests sets up once, as the viewer has it
  # before it checks a script, and the state guard's checks on what the
  # script engines keep for the process.
  add_library(alscript_test_checks OBJECT tests/alscript_test_checks.cpp)
  target_link_libraries(
    alscript_test_checks
    PRIVATE al::flags al_test_support alscript llcommon ll::tailslide
  )
  target_compile_definitions(alscript_test_checks PRIVATE ${definitions_defines})
  set_target_properties(alscript_test_checks PROPERTIES FOLDER "Tests/alscript")

  # One Catch2 binary for alscript's tests, each test file run in a process
  # of its own. The SLua checks, and LSL written again as SLua, run again
  # with Luau's new type solver, which a scripter may choose
  # (ALScriptLuauSolver): the same tests, told by the environment.
  al_add_test_binary(
    alscript_tests
    PROJECT alscript
    SOURCES
      tests/aldiskcache_test.cpp
      tests/aldiskincludes_test.cpp
      tests/alincludeidentity_test.cpp
      tests/alincludesearch_test.cpp
      tests/allinelabel_test.cpp
      tests/allslconsts_test.cpp
      tests/allslcosts_test.cpp
      tests/allsleffects_test.cpp
      tests/allsloptimizer_test.cpp
      tests/allslservice_test.cpp
      tests/allsltoslua_test.cpp
      tests/alluauconfig_test.cpp
      tests/alluaufragment_test.cpp
      tests/alluaunavigation_test.cpp
      tests/alluauservice_test.cpp
      tests/almasterburst_test.cpp
      tests/almasterindex_test.cpp
      tests/almasterlinks_test.cpp
      tests/almastermatch_test.cpp
      tests/almasterplan_test.cpp
      tests/almasterqueue_test.cpp
      tests/almasterwatch_test.cpp
      tests/almessagemap_test.cpp
      tests/almodulelook_test.cpp
      tests/alnotecardformat_test.cpp
      tests/alpreprocessor_test.cpp
      tests/alpreprocessorc11_test.cpp
      tests/alscriptenvelope_test.cpp
      tests/alscriptfixes_test.cpp
      tests/alscriptformatter_test.cpp
      tests/alscripthostile_test.cpp
      tests/alscriptjoblane_test.cpp
      tests/alscriptlexicon_test.cpp
      tests/alscriptlintpass_test.cpp
      tests/alscriptmessages_test.cpp
      tests/alscriptnameindex_test.cpp
      tests/alscriptoutlinepairs_test.cpp
      tests/alscriptsnapshot_test.cpp
      tests/alscriptstack_test.cpp
      tests/alscripttempfiles_test.cpp
      tests/alscripttextcache_test.cpp
      tests/alscriptweight_test.cpp
      tests/alsourcemap_test.cpp
      tests/aluploadheader_test.cpp
    # The preprocessor, the formatter, the optimizer and one analysis of each
    # language over big scripts, with the grid's definitions loaded.
    BENCH_SOURCES tests/alscript_bench.cpp
    SUPPORT alscript_test_checks llcommon_test_checks
    LIBRARIES alscript llcommon ll::slua ll::tailslide
    DEFINES
      ${definitions_defines}
      ${templates_define}
      "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
      "AL_LSCRIPT_REGRESSION_DIR=\"${INDRA_SOURCE_DIR}/lscript/tests/regressions\""
    RERUN newsolver
    RERUN_SOURCES
      tests/alluaufragment_test.cpp
      tests/alluaunavigation_test.cpp
      tests/alluauservice_test.cpp
      tests/allsltoslua_test.cpp
      tests/alscriptfixes_test.cpp
      tests/alscriptlintpass_test.cpp
    RERUN_ENVIRONMENT AL_TEST_LUAU_SOLVER=new
  )

  # libFuzzer on Script Studio, from a script pasted into the editor:
  # alscript_fuzz_lsl runs what a check, the editor's commands and the
  # preprocessor run on one (fuzz/alscriptfuzzlsl.cpp), and
  # alscript_fuzz_format formats one with options the input chooses
  # (fuzz/alscriptfuzzformat.cpp). alscript is built with libFuzzer's
  # coverage for them, whose hooks the sanitizer runtime answers in
  # alscript_tests. Their short runs are tests, over Tailslide's test
  # scripts and the regression scripts, lscript's and alscript's.
  if(AL_BUILD_FUZZERS)
    target_compile_options(alscript PRIVATE -fsanitize=fuzzer-no-link)
    foreach(fuzzer IN ITEMS lsl format)
      add_executable(alscript_fuzz_${fuzzer} fuzz/alscriptfuzz${fuzzer}.cpp)
      target_compile_options(alscript_fuzz_${fuzzer} PRIVATE -fsanitize=fuzzer)
      target_link_options(alscript_fuzz_${fuzzer} PRIVATE -fsanitize=fuzzer)
      target_compile_definitions(alscript_fuzz_${fuzzer} PRIVATE ${definitions_defines})
      target_link_libraries(
        alscript_fuzz_${fuzzer}
        PRIVATE al::flags alscript llcommon ll::slua ll::tailslide
      )
      set_target_properties(alscript_fuzz_${fuzzer} PROPERTIES FOLDER "Tests/alscript")
      # libFuzzer writes the inputs it keeps into the first corpus directory
      # it is given, so that is a scratch one in the build tree: the
      # directories after it are the tests' own scripts, which other tests
      # hold to exactly the files they have.
      set(fuzz_corpus ${CMAKE_CURRENT_BINARY_DIR}/fuzz_corpus/${fuzzer})
      file(MAKE_DIRECTORY ${fuzz_corpus})
      add_test(
        NAME alscript_fuzz_${fuzzer}
        COMMAND
          alscript_fuzz_${fuzzer} -runs=0 -close_fd_mask=1 -max_len=262160 ${fuzz_corpus}
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/tailslide/scripts
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/regressions
          ${INDRA_SOURCE_DIR}/lscript/tests/regressions
      )
    endforeach()
    set_tests_properties(
      alscript_fuzz_lsl
      alscript_fuzz_format
      PROPERTIES
        LABELS "alscript;fuzz"
        ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
        FAIL_REGULAR_EXPRESSION "${AL_TEST_FAIL_REGULAR_EXPRESSION}"
    )
  endif()
endif()
