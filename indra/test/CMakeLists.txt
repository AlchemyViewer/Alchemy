# -*- cmake -*-

# The Catch2 harness: the main, what an LL_ERRS does, LOGFAIL's replay, the
# state guard, the teardown, the matchers, the running case's name and the
# failure type. An OBJECT library, which each test binary links directly: in
# a static archive the listeners' registrations, which nothing references,
# would be dropped by the linker.
add_library(al_test_support OBJECT)

target_sources(
  al_test_support
  PRIVATE
    altestfatal.cpp
    altesthelper.cpp
    altestlinks.cpp
    altestlogreplay.cpp
    altestmain.cpp
    altestmatchers.cpp
    altestname.cpp
    alteststateguard.cpp
    altestteardown.cpp
)

target_sources(
  al_test_support
  PUBLIC
    FILE_SET HEADERS
      FILES
        altestbenchmark.h
        altestfailure.h
        altestfatal.h
        altesthelper.h
        altestlinks.h
        altestlogreplay.h
        altestmatchers.h
        altestname.h
        altestsanitizer.h
        alteststateguard.h
        altestteardown.h
        catch_and_store_what_in.h
        debug.h
        llsdtraits.h
        lltestapp.h
        namedtempfile.h
        print.h
        sync.h
)

# The matchers compare as llmath's is_approx_equal_fraction() does, a header
# function, so the tests need llmath's headers but not its library.
target_include_directories(al_test_support PUBLIC ${INDRA_SOURCE_DIR}/llmath)
target_link_libraries(al_test_support PRIVATE al::flags PUBLIC llcommon ll::catch2)

set_target_properties(al_test_support PROPERTIES FOLDER "Tests")

# The harness's own tests, the first Catch2 binary. Its hidden test cases are
# run one at a time by the entries below, which check the exit code and the
# output CTest sees from the harness: the state guard and what a fixture may
# tell it, LOGFAIL's replay, a crash and an uncaught LL_ERRS, a fixture's end
# failing, the skip and no-match codes, the ISA gate, a listing needing no
# setup, and which runs a benchmark file's cases.
al_add_test_binary(
  test_support_tests
  PROJECT test
  SOURCES tests/altestsupport_test.cpp
  BENCH_SOURCES tests/altestsupport_bench.cpp
  LIBRARIES llcommon
)

function(al_expect_from_test_support name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "CRASHES" "ARGS;EXIT;OUTPUT;REJECT" "")
  set(
    defines
    "-DEXE=$<TARGET_FILE:test_support_tests>"
    "-DARGS=${arg_ARGS}"
    "-DEXPECT_EXIT=${arg_EXIT}"
  )
  if(DEFINED arg_OUTPUT)
    list(APPEND defines "-DEXPECT_OUTPUT=${arg_OUTPUT}")
  endif()
  if(DEFINED arg_REJECT)
    list(APPEND defines "-DREJECT_OUTPUT=${arg_REJECT}")
  endif()
  add_test(
    NAME test_support_tests/${name}
    COMMAND ${CMAKE_COMMAND} ${defines} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_and_expect.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  # A case that CRASHES on purpose is the harness's handler's to report, not
  # a sanitizer's.
  set(modification ${AL_TEST_ENVIRONMENT})
  if(arg_CRASHES)
    list(APPEND modification ${AL_TEST_CRASH_ENVIRONMENT})
  endif()
  # LOGTEST, if a developer has it set, would print every log line and blur
  # what LOGFAIL alone does.
  set_tests_properties(
    test_support_tests/${name}
    PROPERTIES
      ENVIRONMENT "LOGTEST=;LOGFAIL=DEBUG"
      ENVIRONMENT_MODIFICATION "${modification}"
      LABELS test
      TIMEOUT 60
  )
  # A sanitizer's report fails it, as it fails a test binary's entries.
  if(AL_TEST_FAIL_REGULAR_EXPRESSION)
    set_tests_properties(
      test_support_tests/${name}
      PROPERTIES FAIL_REGULAR_EXPRESSION "${AL_TEST_FAIL_REGULAR_EXPRESSION}"
    )
  endif()
endfunction()

al_expect_from_test_support(
  guard_names_the_case
  ARGS "[.guard-leak]"
  EXIT 1
  OUTPUT "a test case that leaves the environment changed. left the environment changed.*AL_TEST_SUPPORT_LEAK=1"
)
# A check a library's own listener adds as the run starts names its culprit
# too. That listener is in every run of this binary, so the entries that
# expect a clean exit show it blames no case that left its state alone,
# whichever listener runs first.
al_expect_from_test_support(
  guard_checks_a_library
  ARGS "[.library-leak]"
  EXIT 1
  OUTPUT "a test case that leaves a library's state changed. left the library's state changed"
)
al_expect_from_test_support(
  replay_on_failure
  ARGS "[.replay-fail]"
  EXIT 42
  OUTPUT "log of .a failing test case that logged.*the line a failing case logged"
)
al_expect_from_test_support(
  no_replay_on_pass
  ARGS "[.replay-pass]"
  EXIT 0
  REJECT "the line a passing case logged"
)
al_expect_from_test_support(
  guard_accepts_a_fixture
  ARGS "[.guard-accept]"
  EXIT 0
  REJECT "State guard"
)
# The directory Windows keeps for each drive, in a hidden variable a change of
# directory there makes, is no variable a test set.
if(WINDOWS)
  al_expect_from_test_support(
    guard_ignores_drive_directories
    ARGS "[.drive-visit]"
    EXIT 0
    REJECT "State guard"
  )
endif()
al_expect_from_test_support(
  replay_on_crash
  CRASHES
  ARGS "[.crash-replay]"
  EXIT nonzero
  OUTPUT "log of .a test case that logged, then crashes.*the line a crashing case logged"
)
al_expect_from_test_support(
  terminate_names_the_case
  CRASHES
  ARGS "[.thread-errs]"
  EXIT nonzero
  OUTPUT "in test case .a test case whose thread hits an LL_ERRS..*an error off the main thread"
)
al_expect_from_test_support(
  teardown_failure_fails
  ARGS "[.teardown-fails]"
  EXIT 1
  OUTPUT "Teardown step threw: LL_ERRS: .*a teardown step that fails"
)
al_expect_from_test_support(all_skipped_is_125 ARGS "[.skip]" EXIT 125)
al_expect_from_test_support(
  guard_fails_a_skipped_run
  ARGS "[.skip-leak]"
  EXIT 1
  OUTPUT "left the environment changed.*AL_TEST_SUPPORT_SKIP_LEAK=1"
)
al_expect_from_test_support(nothing_matched_fails ARGS "no such test case" EXIT 2)
# A test can launch its own binary as a helper process: the main sets the
# process up and runs that helper alone, and no test case.
al_expect_from_test_support(
  helper_runs_alone
  ARGS "--al-helper=altestsupport_exit"
  EXIT 37
  OUTPUT "the helper ran, with APR up"
  REJECT "test cases"
)
al_expect_from_test_support(
  helper_unknown
  ARGS "--al-helper=nonesuch"
  EXIT 2
  OUTPUT "No test helper is named nonesuch"
)
al_expect_from_test_support(
  isa_gate_skips
  ARGS "--isa-level|9"
  EXIT 125
  OUTPUT "does not run x86-64 level 9"
)
# A benchmark file's cases are hidden: the filter a test file's entry runs
# matches none of them, and the one its benchmark entry runs runs them. Few
# samples and no warm-up, since the run, not the number, is checked; Catch2
# still spends about two seconds measuring its clock first.
al_expect_from_test_support(
  benchmark_hidden_from_its_file
  ARGS "[#altestsupport_bench]~[.]"
  EXIT 2
)
al_expect_from_test_support(
  benchmark_runs_from_its_entry
  ARGS "[#altestsupport_bench][!benchmark]|--benchmark-samples|2|--benchmark-warmup-time|1"
  EXIT 0
  OUTPUT "a word's length"
)
# The ISA level is one no host runs: a listing that set up first, or held to
# the gate, would skip rather than list.
al_expect_from_test_support(
  listing_needs_no_setup
  ARGS "--list-tests|--isa-level|9"
  EXIT 0
  OUTPUT "LLSDEquals compares all the way down"
)
