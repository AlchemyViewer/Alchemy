# -*- cmake -*-
#
# Catch2 test binaries: many test files built into one executable, each file
# still run in a process of its own.

include_guard()

if(NOT AL_BUILD_TESTS)
  return()
endif()

# The directories a test's shared libraries are staged in, put on the loader's
# search path for the test's run. macOS takes the same directory as an rpath
# instead, since the loader there ignores the environment.
if(WINDOWS)
  set(AL_TEST_ENVIRONMENT "PATH=path_list_prepend:${SHARED_LIB_STAGING_DIR}")
elseif(LINUX)
  set(AL_TEST_ENVIRONMENT "LD_LIBRARY_PATH=path_list_prepend:${SHARED_LIB_STAGING_DIR}")
else()
  set(AL_TEST_ENVIRONMENT "")
endif()

# A sanitized build's runs. Each sanitizer's options are put before whatever
# its variable already holds, so an option a developer sets in the shell
# comes later and wins. LeakSanitizer, ASan's, looks for leaks as each
# process ends. Catch2 installs a SIGSEGV
# handler of its own to name the case that crashed, which would take the
# report from ASan and TSan, so they keep theirs. A report fails the entry
# whatever the process's exit code: UBSan reports and carries on, and a
# process it reported in still exits 0. Every sanitizer ends a report with a
# SUMMARY line, which the entry's output also holds for a child process whose
# output is the test's. A sanitized run is slower, ThreadSanitizer's most of
# all, and the timeouts are scaled to it. Suppressions are in test/sanitizers.
#
# AL_TEST_CRASH_ENVIRONMENT goes after that, for an entry whose case crashes
# on purpose to check what the harness makes of a crash: there the sanitizers
# leave SIGSEGV to Catch2's handler, and ThreadSanitizer does not report the
# allocations that handler makes, as a crash's handler can hardly avoid.
set(AL_TEST_SANITIZER_ENVIRONMENT "")
set(AL_TEST_CRASH_ENVIRONMENT "")
set(AL_TEST_FAIL_REGULAR_EXPRESSION "")
set(AL_TEST_TIMEOUT_SCALE 1)
if(AL_SANITIZING)
  set(suppressions "${INDRA_SOURCE_DIR}/test/sanitizers")
  set(AL_TEST_FAIL_REGULAR_EXPRESSION "SUMMARY: [A-Za-z]+Sanitizer: ")
  if("undefined" IN_LIST AL_SANITIZERS)
    list(
      APPEND AL_TEST_SANITIZER_ENVIRONMENT
      "UBSAN_OPTIONS=string_prepend:print_stacktrace=1:report_error_type=1:suppressions='${suppressions}/ubsan.supp':"
    )
    set(AL_TEST_TIMEOUT_SCALE 2)
  endif()
  if("address" IN_LIST AL_SANITIZERS)
    list(
      APPEND AL_TEST_SANITIZER_ENVIRONMENT
      "ASAN_OPTIONS=string_prepend:detect_leaks=1:allow_user_segv_handler=0:check_initialization_order=1:"
      "LSAN_OPTIONS=string_prepend:suppressions='${suppressions}/lsan.supp':print_suppressions=0:"
    )
    list(APPEND AL_TEST_CRASH_ENVIRONMENT "ASAN_OPTIONS=string_append::handle_segv=0")
    set(AL_TEST_TIMEOUT_SCALE 3)
  endif()
  if("thread" IN_LIST AL_SANITIZERS)
    list(
      APPEND AL_TEST_SANITIZER_ENVIRONMENT
      "TSAN_OPTIONS=string_prepend:allow_user_segv_handler=0:second_deadlock_stack=1:suppressions='${suppressions}/tsan.supp':"
    )
    list(
      APPEND AL_TEST_CRASH_ENVIRONMENT
      "TSAN_OPTIONS=string_append::handle_segv=0:report_signal_unsafe=0"
    )
    set(AL_TEST_TIMEOUT_SCALE 10)
  endif()
  list(APPEND AL_TEST_ENVIRONMENT ${AL_TEST_SANITIZER_ENVIRONMENT})
  unset(suppressions)
endif()

# al_add_test_binary(<name> PROJECT <project> SOURCES <test file>... [UNIT <file>...]
#                    [LIBRARIES <target>...] [SUPPORT <object library>...]
#                    [INCLUDES <dir>...] [DEFINES <define>...] [ENVIRONMENT <VAR=value>...]
#                    [GL] [PYTHON] [ISA_TIER <tier>] [RESOURCE_LOCK <name>...] [LAUNCHER <command>...]
#                    [RERUN <run> RERUN_SOURCES <test file>... RERUN_ENVIRONMENT <VAR=value>...]
#                    [BENCH_SOURCES <benchmark file>...] [COMBINED])
#
# Builds the test files into one Catch2 executable, <name>, with the harness
# (al_test_support: the main, the listeners and the matchers) and SUPPORT's
# objects, linked against LIBRARIES. Anything that registers a test case or a
# listener is a source of the executable or of an OBJECT library: in a static
# archive the linker drops what nothing references, and the registrations
# with it, so the tests would vanish without a word. DEFINES and INCLUDES are
# the binary's, for every file in it.
#
# UNIT names files of PROJECT, the library, compiled into the binary instead
# of linked, for a test that stubs what they call: its stubs would duplicate
# the library's own definitions. The binary sees the library's include
# directories, and LIBRARIES is all it links. Such a test is a binary of its
# own, since every other test links the code it tests.
#
# Each test file is a CTest entry, <name>/<file stem>, that runs the file's
# test cases -- chosen by the tag the main gives each case from the name of
# its file, as Catch2's --filenames-as-tags does -- in one process, as an
# executable to a test file ran them. Naming a tag would also choose the
# file's hidden cases, such as its benchmarks, so the filter leaves those out,
# as a run with no filter does. A filter that matches nothing exits 2, so a
# file whose test cases went missing fails rather than passing empty. A stem
# names one file in a binary, and a file belongs to one binary, or to one at
# each ISA tier. The tag is the file's name less its last extension, the stem
# here too. A case written in a header, or in a SUPPORT library, is tagged
# with that file's name and no file's entry would run it, so
# <name>/stray_cases fails when the binary has one: it runs whatever no file's
# tag names, and expects nothing to run.
#
# <name>/combined runs all of them in one process. It is labelled
# combined_pending and manual, which the default run leaves out, until the
# binary's tests are known to share a process: then COMBINED labels it
# combined, and it runs with the rest. A binary of one file has none: that
# file's entry already runs every case in one process. With AL_TEST_PER_CASE,
# each test case is an entry of its own instead, found when ctest runs, with
# the environment and labels a file's entry would have.
#
# ENVIRONMENT sets variables for the runs, VAR=value each. GL, PYTHON and
# ISA_TIER mean what they mean to al_add_test: a GL binary is labelled gl and
# registered disabled without AL_ENABLE_GL_TESTS, a PYTHON binary is given the
# interpreter and registered disabled without one that has llsd, and an
# ISA_TIER binary is built at that x86-64 tier, named <name>_<tier>, labelled
# isa, and skips on a host below it -- or below the tree's own tier, since it
# links the tree's libraries, built at that one. RESOURCE_LOCK names what the binary's
# entries hold that no two may hold at once, such as a fixed port. LAUNCHER is
# a command each entry runs the binary through, its path and arguments after
# the command's own: llcorehttp's tests run under the Python peer that serves
# their requests. A per-case entry, and its discovery, go through it too.
#
# RERUN runs some of the files a second time, each in a process of its own,
# with RERUN_ENVIRONMENT added to the binary's: <name>_<run>/<file stem>, or
# with AL_TEST_PER_CASE each of their cases as <name>_<run>/<case>. alscript's
# tests that check SLua run again under Luau's new type solver, as a scripter
# may choose it.
#
# BENCH_SOURCES are benchmark files, built into the binary beside the tests,
# whose cases are tagged [!benchmark]: hidden, so no test file's entry, the
# combined run or the per-case entries run them. Each is an entry of its
# own, bench/<name>/<file stem>, labelled benchmark and manual, which the
# default run leaves out and BUILD_AND_RUN_BENCHMARKS runs. It runs alone,
# since anything beside it would move its numbers, and reports to the
# console and as XML, Testing/benchmarks/<name>_<file stem>.xml, which keeps
# the numbers where Catch2's JSON reporter would drop them. A benchmark case
# skips outside an optimised build (altestbenchmark.h), and the entry then
# reads as skipped.
function(al_add_test_binary name)
  cmake_parse_arguments(
    PARSE_ARGV 1
    arg
    "GL;PYTHON;COMBINED"
    "PROJECT;ISA_TIER;RERUN"
    "SOURCES;UNIT;LIBRARIES;SUPPORT;INCLUDES;DEFINES;ENVIRONMENT;RESOURCE_LOCK;LAUNCHER;RERUN_SOURCES;RERUN_ENVIRONMENT;BENCH_SOURCES"
  )
  if(NOT arg_PROJECT)
    message(FATAL_ERROR "al_add_test_binary(${name}): PROJECT is required")
  endif()
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "al_add_test_binary(${name}): SOURCES is required")
  endif()
  if(arg_UNPARSED_ARGUMENTS)
    message(
      FATAL_ERROR
      "al_add_test_binary(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}"
    )
  endif()
  if(arg_RERUN_SOURCES AND NOT arg_RERUN)
    message(FATAL_ERROR "al_add_test_binary(${name}): RERUN_SOURCES needs RERUN to name the run")
  endif()
  if(arg_RERUN AND NOT arg_RERUN_SOURCES)
    message(FATAL_ERROR "al_add_test_binary(${name}): RERUN ${arg_RERUN} names no RERUN_SOURCES")
  endif()
  set(target ${name})
  if(arg_ISA_TIER)
    if(NOT arg_ISA_TIER MATCHES "^(baseline|v2|v3|v4)$")
      message(
        FATAL_ERROR
        "al_add_test_binary(${name}): ISA_TIER must be baseline, v2, v3 or v4, not ${arg_ISA_TIER}"
      )
    endif()
    set(target ${name}_${arg_ISA_TIER})
  endif()

  set(stems)
  set(bench_stems)
  foreach(source IN LISTS arg_SOURCES arg_BENCH_SOURCES)
    get_filename_component(stem "${source}" NAME_WLE)
    if(stem IN_LIST stems OR stem IN_LIST bench_stems)
      message(
        FATAL_ERROR
        "al_add_test_binary(${name}): two test files are named ${stem}; the stem names a file's entry and its tag"
      )
    endif()
    if(source IN_LIST arg_BENCH_SOURCES)
      list(APPEND bench_stems ${stem})
    else()
      list(APPEND stems ${stem})
    endif()

    # A file is in one binary at each tier: the variants of an ISA_TIER
    # binary build the same files at the others.
    get_filename_component(path "${source}" ABSOLUTE)
    string(MD5 key "${path}${arg_ISA_TIER}")
    get_property(owner GLOBAL PROPERTY AL_TEST_SOURCE_${key})
    if(owner)
      message(
        FATAL_ERROR
        "al_add_test_binary(${name}): ${source} is already in ${owner}; a test file in two binaries would run twice"
      )
    endif()
    set_property(GLOBAL PROPERTY AL_TEST_SOURCE_${key} ${target})
  endforeach()
  set(rerun_stems)
  foreach(source IN LISTS arg_RERUN_SOURCES)
    get_filename_component(stem "${source}" NAME_WLE)
    if(NOT stem IN_LIST stems)
      message(
        FATAL_ERROR
        "al_add_test_binary(${name}): RERUN_SOURCES' ${source} is not one of its SOURCES"
      )
    endif()
    list(APPEND rerun_stems ${stem})
  endforeach()

  # vcpkg's toolchain hangs an app-local copy step on every executable it
  # sees declared; a test runs from the build tree, as al_add_test explains.
  set(VCPKG_APPLOCAL_DEPS OFF)
  add_executable(${target} ${arg_SOURCES} ${arg_BENCH_SOURCES} ${arg_UNIT})
  target_link_libraries(${target} PRIVATE al::flags al_test_support ${arg_SUPPORT} ${arg_LIBRARIES})
  target_include_directories(${target} PRIVATE ${arg_INCLUDES} ${INDRA_SOURCE_DIR}/test)
  if(arg_UNIT)
    target_include_directories(
      ${target}
      PRIVATE $<TARGET_PROPERTY:${arg_PROJECT},INTERFACE_INCLUDE_DIRECTORIES>
    )
  endif()
  if(arg_DEFINES)
    target_compile_definitions(${target} PRIVATE ${arg_DEFINES})
  endif()
  set_target_properties(${target} PROPERTIES FOLDER "Tests/${arg_PROJECT}")
  if(arg_ISA_TIER)
    set_target_properties(${target} PROPERTIES AL_ISA_TIER ${arg_ISA_TIER})
  endif()
  if(WINDOWS)
    set_target_properties(${target} PROPERTIES AL_SKIP_RELEASE_DEBUG_INFO ON)
  elseif(DARWIN)
    # A test binary is run straight from the build tree, so it is ad-hoc signed.
    set_target_properties(
      ${target}
      PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-" BUILD_RPATH "${SHARED_LIB_STAGING_DIR}"
    )
  endif()

  set(arguments)
  set(environment ${arg_ENVIRONMENT})
  set(labels ${arg_PROJECT})
  set(disabled FALSE)
  # As al_add_test's: five minutes a test file, fifteen with a Python peer.
  set(timeout 300)
  if(arg_ISA_TIER)
    al_isa_level(${arg_ISA_TIER} ${CMAKE_SYSTEM_NAME} ${ARCH} level)
    al_isa_level(${AL_ISA_TIER} ${CMAKE_SYSTEM_NAME} ${ARCH} tree_level)
    if(tree_level GREATER level)
      set(level ${tree_level})
    endif()
    # One argument, not two: catch_discover_tests hands EXTRA_ARGS to each
    # case as one, joined with ;.
    list(APPEND arguments --isa-level=${level})
    list(APPEND labels isa)
  endif()
  if(arg_PYTHON)
    set(timeout 900)
    list(APPEND labels python)
    if(AL_PYTHON_PEERS)
      list(APPEND environment "PYTHON=${Python3_EXECUTABLE}")
    else()
      set(disabled TRUE)
    endif()
  endif()
  if(arg_GL)
    list(APPEND labels gl)
    if(NOT AL_ENABLE_GL_TESTS)
      set(disabled TRUE)
    endif()
  endif()
  math(EXPR timeout "${timeout} * ${AL_TEST_TIMEOUT_SCALE}")

  # The runs, each a prefix to its entries' names: the binary's own, and
  # RERUN's of its files. The entries registered here, as opposed to found
  # per case, are gathered for the properties every one of them takes.
  set(prefixes ${target})
  set(entries)
  if(arg_RERUN)
    list(APPEND prefixes ${target}_${arg_RERUN})
  endif()

  if(AL_TEST_PER_CASE)
    # Beside Catch2's package config, where vcpkg puts it, and not on the
    # module path.
    include("${Catch2_DIR}/Catch.cmake")
    # WORKING_DIRECTORY is catch_discover_tests' own: among the PROPERTIES it
    # would end them, and what came after would go unset.
    set(
      per_case_properties
      TIMEOUT
      ${timeout}
      SKIP_RETURN_CODE
      125
      DISABLED
      ${disabled}
    )
    # Catch.cmake runs the discovery and each case through the target's
    # emulator, which CMake's own add_test leaves alone in a native build.
    if(arg_LAUNCHER)
      set_target_properties(${target} PROPERTIES CROSSCOMPILING_EMULATOR "${arg_LAUNCHER}")
    endif()
    foreach(prefix IN LISTS prefixes)
      # A rerun's cases are its files', by the tag each file's name gives
      # them, its hidden ones left out as the binary's own run leaves them.
      set(spec)
      set(run_environment ${environment})
      if(NOT prefix STREQUAL target)
        set(files)
        foreach(stem IN LISTS rerun_stems)
          list(APPEND files "[#${stem}]~[.]")
        endforeach()
        list(JOIN files "," files)
        set(spec "${files}")
        list(APPEND run_environment ${arg_RERUN_ENVIRONMENT})
      endif()
      # The main makes Catch2's "all skipped" CTest's 125, so a skip is not
      # left to the discovery's own code for it.
      catch_discover_tests(
        ${target}
        TEST_PREFIX "${prefix}/"
        TEST_LIST ${prefix}_cases
        TEST_SPEC ${spec}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        EXTRA_ARGS ${arguments}
        DISCOVERY_MODE PRE_TEST
        SKIP_IS_FAILURE
        DL_PATHS ${SHARED_LIB_STAGING_DIR}
        PROPERTIES ${per_case_properties}
      )
      # The environment, labels and lock a file's entry has are lists, which
      # catch_discover_tests flattens with the rest of its PROPERTIES, and a
      # CTest script cannot append to a property: a script run after the
      # discovery sets them on each case whole. The labels are a file entry's,
      # not the cases' Catch2 tags, so that -L and -LE choose alike either way.
      # The library path is DL_PATHS', and not AL_TEST_ENVIRONMENT's, whose
      # generator expressions a script written here would leave unevaluated.
      # The sanitizers' options are set whole, not put before the shell's.
      set(case_script "${CMAKE_CURRENT_BINARY_DIR}/${prefix}_cases.cmake")
      set(case_properties "LABELS [==[${labels}]==]")
      foreach(variable IN LISTS AL_TEST_SANITIZER_ENVIRONMENT)
        string(REPLACE "=string_prepend:" "=" variable "${variable}")
        list(APPEND run_environment "${variable}")
      endforeach()
      if(AL_TEST_FAIL_REGULAR_EXPRESSION)
        string(
          APPEND case_properties
          " FAIL_REGULAR_EXPRESSION [==[${AL_TEST_FAIL_REGULAR_EXPRESSION}]==]"
        )
      endif()
      if(run_environment)
        string(APPEND case_properties " ENVIRONMENT [==[${run_environment}]==]")
      endif()
      if(arg_RESOURCE_LOCK)
        string(APPEND case_properties " RESOURCE_LOCK [==[${arg_RESOURCE_LOCK}]==]")
      endif()
      # The list holds each name as Catch2 escaped it for a list: [ and ] as
      # the bytes 2 and 3. The test is registered under the name itself.
      file(
        WRITE "${case_script}"
        "string(ASCII 2 open)\n"
        "string(ASCII 3 close)\n"
        "foreach(case IN LISTS ${prefix}_cases)\n"
        "  string(REPLACE \"\${open}\" \"[\" case \"\${case}\")\n"
        "  string(REPLACE \"\${close}\" \"]\" case \"\${case}\")\n"
        "  set_tests_properties(\"\${case}\" PROPERTIES ${case_properties})\n"
        "endforeach()\n"
      )
      set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${case_script}")
    endforeach()
  else()
    foreach(prefix IN LISTS prefixes)
      set(run_stems ${stems})
      set(run_environment ${environment})
      if(NOT prefix STREQUAL target)
        set(run_stems ${rerun_stems})
        list(APPEND run_environment ${arg_RERUN_ENVIRONMENT})
      endif()
      foreach(stem IN LISTS run_stems)
        list(APPEND entries ${prefix}/${stem})
        add_test(
          NAME ${prefix}/${stem}
          COMMAND ${arg_LAUNCHER} $<TARGET_FILE:${target}> "[#${stem}]~[.]" ${arguments}
          WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
        set_tests_properties(
          ${prefix}/${stem}
          PROPERTIES
            ENVIRONMENT "${run_environment}"
            ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
            LABELS "${labels}"
            DISABLED ${disabled}
            SKIP_RETURN_CODE 125
            TIMEOUT ${timeout}
        )
        if(arg_RESOURCE_LOCK)
          set_tests_properties(${prefix}/${stem} PROPERTIES RESOURCE_LOCK "${arg_RESOURCE_LOCK}")
        endif()
      endforeach()
    endforeach()

    # Every case no file's tag names: a filter of nothing but exclusions
    # leaves the hidden cases out, as the files' entries do, and one that
    # matches nothing exits 2. The ISA gate is not asked: nothing runs, and a
    # listing already needs the binary to start on any host. Disabled where
    # the files' entries are: a run starts its checks whatever it matches,
    # and a GL binary's make the context the host has none of.
    math(EXPR stray_timeout "60 * ${AL_TEST_TIMEOUT_SCALE}")
    set(strays)
    foreach(stem IN LISTS stems)
      string(APPEND strays "~[#${stem}]")
    endforeach()
    list(APPEND entries ${target}/stray_cases)
    add_test(
      NAME ${target}/stray_cases
      COMMAND
        ${CMAKE_COMMAND} "-DEXE=$<TARGET_FILE:${target}>" "-DARGS=${strays}~[.]" -DEXPECT_EXIT=2 -P
        ${INDRA_SOURCE_DIR}/test/run_and_expect.cmake
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(
      ${target}/stray_cases
      PROPERTIES
        ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
        LABELS "${labels}"
        DISABLED ${disabled}
        TIMEOUT ${stray_timeout}
    )

    list(LENGTH stems file_count)
    if(file_count GREATER 1)
      # As long as the files take, but no longer than an hour, or three in a
      # sanitized run: a hang, which the combined run is there to find, holds
      # one CI row only so long, and the row's own limit is past that.
      math(EXPR combined_timeout "${timeout} * ${file_count}")
      set(combined_cap 3600)
      if(AL_TEST_TIMEOUT_SCALE GREATER 1)
        set(combined_cap 10800)
      endif()
      if(combined_timeout GREATER combined_cap)
        set(combined_timeout ${combined_cap})
      endif()
      set(combined_label "combined_pending;manual")
      if(arg_COMBINED)
        set(combined_label combined)
      endif()
      list(APPEND entries ${target}/combined)
      add_test(
        NAME ${target}/combined
        COMMAND ${arg_LAUNCHER} $<TARGET_FILE:${target}> ${arguments}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      )
      set_tests_properties(
        ${target}/combined
        PROPERTIES
          ENVIRONMENT "${environment}"
          ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
          LABELS "${labels};${combined_label}"
          DISABLED ${disabled}
          SKIP_RETURN_CODE 125
          TIMEOUT ${combined_timeout}
      )
      if(arg_RESOURCE_LOCK)
        set_tests_properties(${target}/combined PROPERTIES RESOURCE_LOCK "${arg_RESOURCE_LOCK}")
      endif()
    endif()
  endif()

  # A benchmark file's run, whichever way the tests are registered. It names
  # the file's tag and Catch2's benchmark tag together, which chooses the
  # hidden cases a tag alone would leave out.
  if(arg_BENCH_SOURCES)
    set(bench_dir "${CMAKE_BINARY_DIR}/Testing/benchmarks")
    file(MAKE_DIRECTORY "${bench_dir}")
    foreach(stem IN LISTS bench_stems)
      list(APPEND entries bench/${target}/${stem})
      add_test(
        NAME bench/${target}/${stem}
        COMMAND
          ${arg_LAUNCHER} $<TARGET_FILE:${target}> "[#${stem}][!benchmark]" --reporter console
          --reporter "xml::out=${bench_dir}/${target}_${stem}.xml" ${arguments}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      )
      set_tests_properties(
        bench/${target}/${stem}
        PROPERTIES
          ENVIRONMENT "${environment}"
          ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
          LABELS "${labels};benchmark;manual"
          DISABLED ${disabled}
          SKIP_RETURN_CODE 125
          RUN_SERIAL TRUE
          TIMEOUT 3600
      )
    endforeach()
    add_dependencies(BUILD_BENCHMARKS ${target})
  endif()

  if(AL_TEST_FAIL_REGULAR_EXPRESSION AND entries)
    set_tests_properties(
      ${entries}
      PROPERTIES FAIL_REGULAR_EXPRESSION "${AL_TEST_FAIL_REGULAR_EXPRESSION}"
    )
  endif()

  add_dependencies(BUILD_TESTS ${target})
endfunction()
