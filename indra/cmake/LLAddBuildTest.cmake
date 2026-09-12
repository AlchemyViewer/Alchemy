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

# al_add_test(<name> PROJECT <project> [UNIT]
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
# run, VAR=value each; PYTHON is always set to the interpreter CMake found,
# for the tests that spawn a Python peer.
#
# Targets are PROJECT_<project>_TEST_<name> for a unit test and
# INTEGRATION_TEST_<name> otherwise; the registered test names are
# PROJECT_<project>_TEST_<name> and INTEGRATION_TEST_RUNNER_<name>.
function(al_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg
    "UNIT"
    "PROJECT"
    "SOURCES;LIBRARIES;INCLUDES;DEFINES;COMMAND;ENVIRONMENT")
  if(NOT arg_PROJECT)
    message(FATAL_ERROR "al_add_test(${name}): PROJECT is required")
  endif()
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "al_add_test(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  if(arg_UNIT)
    set(target PROJECT_${arg_PROJECT}_TEST_${name})
    set(test_name ${target})
    set(sources ${name}.cpp tests/${name}_test.cpp ${arg_SOURCES} ${name}.h)
    set(libraries lltut_runner_lib llcommon ll::tut)
    if(NOT arg_PROJECT STREQUAL "llmath")
      list(APPEND libraries llmath)
    endif()
  else()
    set(target INTEGRATION_TEST_${name})
    set(test_name INTEGRATION_TEST_RUNNER_${name})
    set(sources tests/${name}_test.cpp ${arg_SOURCES})
    set(libraries lltut_runner_lib ll::tut)
  endif()

  add_executable(${target} ${sources})
  target_link_libraries(${target} PRIVATE al::flags ${libraries} ${arg_LIBRARIES})
  if(arg_UNIT)
    # The project's include directories, since the test compiles one of its
    # files without linking it.
    get_property(project_includes TARGET ${arg_PROJECT} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(${target} PRIVATE ${project_includes})
  endif()
  target_include_directories(${target} PRIVATE
    ${arg_INCLUDES}
    ${INDRA_SOURCE_DIR}/test
    ${INDRA_SOURCE_DIR}/llmath
    ${INDRA_SOURCE_DIR}/llui)
  target_compile_definitions(${target} PRIVATE
    "LL_TEST=${name}"
    "LL_TEST_${name}"
    ${arg_DEFINES})
  set_target_properties(${target} PROPERTIES FOLDER "Tests/${arg_PROJECT}")

  if(WINDOWS)
    set_target_properties(${target} PROPERTIES AL_SKIP_RELEASE_DEBUG_INFO ON)
  elseif(DARWIN)
    # A test binary is run straight from the build tree, so it is ad-hoc signed.
    set_target_properties(${target} PROPERTIES
      XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
      BUILD_RPATH "${SHARED_LIB_STAGING_DIR}")
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
    list(APPEND command
      "--touch=${CMAKE_CURRENT_BINARY_DIR}/${target}_ok.txt"
      "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  set(environment "PYTHON=${Python3_EXECUTABLE}" ${arg_ENVIRONMENT})
  add_test(NAME ${test_name}
    COMMAND ${command}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(${test_name} PROPERTIES
    ENVIRONMENT "${environment}"
    ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}")

  add_dependencies(BUILD_TESTS ${target})
endfunction()
