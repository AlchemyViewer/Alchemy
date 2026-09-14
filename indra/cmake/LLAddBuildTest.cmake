# -*- cmake -*-
#
# The test harness. One function declares a TUT test executable and
# registers it with CTest.

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

# al_add_test(<name> PROJECT <project> [UNIT] [PYTHON] [GL] [ISA_TIER <tier>]
#             [SOURCES <file>...] [LIBRARIES <target>...] [INCLUDES <dir>...]
#             [DEFINES <define>...] [COMMAND <arg>...] [ENVIRONMENT <VAR=value>...])
#
# Builds tests/<name>_test.cpp into an executable and registers it as a test
# of <project>.
#
# A UNIT test compiles the project's own <name>.cpp into the executable and
# sees the project's include directories instead of linking the project, so
# the test decides what that one file links against; <name>.h is listed with
# it. Any other test links whatever LIBRARIES names. Both link the TUT runner,
# and a unit test also links llcommon and llmath, the libraries every source
# file in the tree reaches for.
#
# COMMAND runs the test through another program; "{}" stands for the test
# executable and is appended when absent. ENVIRONMENT sets variables for the
# run, VAR=value each. A PYTHON test spawns a Python peer: PYTHON is set to
# the interpreter for its run, and without one the test is registered
# disabled. A GL test stands up a GL context on a hidden window: it is
# labelled gl, and where AL_ENABLE_GL_TESTS is off it is built and
# registered disabled.
#
# ISA_TIER builds the test for that x86-64 tier (baseline, v2, v3 or v4)
# rather than the tree's, against the tree's libraries: its own sources are
# compiled with that tier's flag and AL_ISA_LEVEL, so a SIMD path can be
# tested at every tier from one configure. The target and test names carry
# _<tier>, the test is labelled isa, and it exits 125 -- skipped, to CTest --
# on a host that cannot run the tier. It links the tree's libraries, which
# were compiled at the tree's tier, so on a host below the tree's tier it
# skips whatever its own tier is.
#
# Targets are PROJECT_<project>_TEST_<name> for a unit test and
# INTEGRATION_TEST_<name> otherwise; the registered test names are
# PROJECT_<project>_TEST_<name> and INTEGRATION_TEST_RUNNER_<name>.
function(al_add_test name)
  cmake_parse_arguments(
    PARSE_ARGV 1
    arg
    "UNIT;PYTHON;GL"
    "PROJECT;ISA_TIER"
    "SOURCES;LIBRARIES;INCLUDES;DEFINES;COMMAND;ENVIRONMENT"
  )
  if(NOT arg_PROJECT)
    message(FATAL_ERROR "al_add_test(${name}): PROJECT is required")
  endif()
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "al_add_test(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  set(suffix "")
  if(arg_ISA_TIER)
    if(NOT arg_ISA_TIER MATCHES "^(baseline|v2|v3|v4)$")
      message(
        FATAL_ERROR
        "al_add_test(${name}): ISA_TIER must be baseline, v2, v3 or v4, not ${arg_ISA_TIER}"
      )
    endif()
    set(suffix "_${arg_ISA_TIER}")
  endif()

  if(arg_UNIT)
    set(target PROJECT_${arg_PROJECT}_TEST_${name}${suffix})
    set(test_name ${target})
    set(sources ${name}.cpp tests/${name}_test.cpp ${arg_SOURCES} ${name}.h)
    set(libraries lltut_runner_lib llcommon ll::tut)
    if(NOT arg_PROJECT STREQUAL "llmath")
      list(APPEND libraries llmath)
    endif()
  else()
    set(target INTEGRATION_TEST_${name}${suffix})
    set(test_name INTEGRATION_TEST_RUNNER_${name}${suffix})
    set(sources tests/${name}_test.cpp ${arg_SOURCES})
    set(libraries lltut_runner_lib ll::tut)
  endif()

  # vcpkg's toolchain hangs an app-local copy step on every executable it
  # sees declared. A test runs from the build tree with its libraries on the
  # search path above, so the step has nothing to copy, and launching vcpkg
  # once per test fails now and then under a parallel build when the
  # executable is already open. Off for the scope of this declaration.
  set(VCPKG_APPLOCAL_DEPS OFF)
  add_executable(${target} ${sources})
  target_link_libraries(${target} PRIVATE al::flags ${libraries} ${arg_LIBRARIES})
  if(arg_UNIT)
    # The project's include directories, since the test compiles one of its
    # files without linking it.
    get_property(project_includes TARGET ${arg_PROJECT} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(${target} PRIVATE ${project_includes})
  endif()
  target_include_directories(
    ${target}
    PRIVATE
      ${arg_INCLUDES}
      ${INDRA_SOURCE_DIR}/test
      ${INDRA_SOURCE_DIR}/llmath
      ${INDRA_SOURCE_DIR}/llui
  )
  target_compile_definitions(${target} PRIVATE "LL_TEST=${name}" "LL_TEST_${name}" ${arg_DEFINES})
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

  set(command ${arg_COMMAND})
  list(FIND command "{}" executable_position)
  if(executable_position LESS 0)
    list(APPEND command "$<TARGET_FILE:${target}>")
  else()
    list(REMOVE_AT command ${executable_position})
    list(INSERT command ${executable_position} "$<TARGET_FILE:${target}>")
  endif()
  if(arg_UNIT)
    list(
      APPEND command
      "--touch=${CMAKE_CURRENT_BINARY_DIR}/${target}_ok.txt"
      "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endif()

  set(environment ${arg_ENVIRONMENT})
  set(labels)
  set(disabled FALSE)
  if(arg_ISA_TIER)
    al_isa_level(${arg_ISA_TIER} ${CMAKE_SYSTEM_NAME} ${ARCH} level)
    list(APPEND command "--isa-level=${level}")
    list(APPEND labels isa)
  endif()
  if(arg_PYTHON)
    if(Python3_Interpreter_FOUND)
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
  add_test(NAME ${test_name} COMMAND ${command} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(
    ${test_name}
    PROPERTIES
      ENVIRONMENT "${environment}"
      ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
      LABELS "${labels}"
      DISABLED ${disabled}
      SKIP_RETURN_CODE 125
  )

  add_dependencies(BUILD_TESTS ${target})
endfunction()

# al_add_bench(<name> PROJECT <project> [SOURCES <file>...] [LIBRARIES <target>...]
#              [INCLUDES <dir>...] [DEFINES <define>...])
#
# Builds tests/<name>_bench.cpp, which brings its own main, into
# BENCH_<name> and registers it as a test labelled benchmark. The default
# test run leaves that label out; BUILD_AND_RUN_BENCHMARKS runs it, verbose,
# since the numbers are the output. A benchmark exits 125, which CTest reads
# as skipped, from a build whose numbers would say nothing.
function(al_add_bench name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "PROJECT" "SOURCES;LIBRARIES;INCLUDES;DEFINES")
  if(NOT arg_PROJECT)
    message(FATAL_ERROR "al_add_bench(${name}): PROJECT is required")
  endif()
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "al_add_bench(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  set(target BENCH_${name})
  set(VCPKG_APPLOCAL_DEPS OFF)
  add_executable(${target} tests/${name}_bench.cpp ${arg_SOURCES})
  target_link_libraries(${target} PRIVATE al::flags ${arg_LIBRARIES})
  target_include_directories(${target} PRIVATE ${arg_INCLUDES} ${INDRA_SOURCE_DIR}/llmath)
  target_compile_definitions(${target} PRIVATE "AL_BENCH=1" ${arg_DEFINES})
  set_target_properties(${target} PROPERTIES FOLDER "Benchmarks/${arg_PROJECT}")
  if(WINDOWS)
    set_target_properties(${target} PROPERTIES AL_SKIP_RELEASE_DEBUG_INFO ON)
  elseif(DARWIN)
    set_target_properties(
      ${target}
      PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-" BUILD_RPATH "${SHARED_LIB_STAGING_DIR}"
    )
  endif()

  add_test(
    NAME ${target}
    COMMAND $<TARGET_FILE:${target}>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(
    ${target}
    PROPERTIES
      ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
      LABELS benchmark
      SKIP_RETURN_CODE 125
  )

  add_dependencies(BUILD_BENCHMARKS ${target})
endfunction()
