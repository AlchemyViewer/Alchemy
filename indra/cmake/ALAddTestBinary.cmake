# -*- cmake -*-
#
# Catch2 test binaries: many test files built into one executable, each file
# still run in a process of its own.

include_guard()

if(NOT AL_BUILD_TESTS)
  return()
endif()

# al_add_test_binary(<name> PROJECT <project> SOURCES <test file>...
#                    [LIBRARIES <target>...] [SUPPORT <object library>...]
#                    [INCLUDES <dir>...] [DEFINES <define>...] [ENVIRONMENT <VAR=value>...]
#                    [GL] [PYTHON] [ISA_TIER <tier>] [RESOURCE_LOCK <name>...] [COMBINED])
#
# Builds the test files into one Catch2 executable, <name>, with the harness
# (al_test_support: the main, the listeners and the matchers) and SUPPORT's
# objects, linked against LIBRARIES. Anything that registers a test case or a
# listener is a source of the executable or of an OBJECT library: in a static
# archive the linker drops what nothing references, and the registrations
# with it, so the tests would vanish without a word. DEFINES and INCLUDES are
# the binary's, for every file in it.
#
# Each test file is a CTest entry, <name>/<file stem>, that runs the file's
# test cases -- chosen by the tag Catch2's --filenames-as-tags gives each case
# from the name of its file -- in one process, as an executable to a test file
# ran them. Naming a tag would also choose the file's hidden cases, such as its
# benchmarks, so the filter leaves those out, as a run with no filter does. A
# filter that matches nothing exits 2, so a file whose test cases went missing
# fails rather than passing empty. A stem names one file in a binary, and a
# file belongs to one binary, or to one at each ISA tier.
#
# <name>/combined runs all of them in one process. It is labelled
# combined_pending and manual, which the default run leaves out, until the
# binary's tests are known to share a process: then COMBINED labels it
# combined, and it runs with the rest. With AL_TEST_PER_CASE, each test case is
# an entry of its own instead, found when ctest runs, with the environment and
# labels a file's entry would have.
#
# ENVIRONMENT sets variables for the runs, VAR=value each. GL, PYTHON and
# ISA_TIER mean what they mean to al_add_test: a GL binary is labelled gl and
# registered disabled without AL_ENABLE_GL_TESTS, a PYTHON binary is given the
# interpreter and registered disabled without one that has llsd, and an
# ISA_TIER binary is built at that x86-64 tier, named <name>_<tier>, labelled
# isa, and skips on a host below it -- or below the tree's own tier, since it
# links the tree's libraries, built at that one. RESOURCE_LOCK names what the binary's
# entries hold that no two may hold at once, such as a fixed port.
function(al_add_test_binary name)
  cmake_parse_arguments(
    PARSE_ARGV 1
    arg
    "GL;PYTHON;COMBINED"
    "PROJECT;ISA_TIER"
    "SOURCES;LIBRARIES;SUPPORT;INCLUDES;DEFINES;ENVIRONMENT;RESOURCE_LOCK"
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
  foreach(source IN LISTS arg_SOURCES)
    get_filename_component(stem "${source}" NAME_WE)
    if(stem IN_LIST stems)
      message(
        FATAL_ERROR
        "al_add_test_binary(${name}): two test files are named ${stem}; the stem names a file's entry and its tag"
      )
    endif()
    list(APPEND stems ${stem})

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

  # vcpkg's toolchain hangs an app-local copy step on every executable it
  # sees declared; a test runs from the build tree, as al_add_test explains.
  set(VCPKG_APPLOCAL_DEPS OFF)
  add_executable(${target} ${arg_SOURCES})
  target_link_libraries(${target} PRIVATE al::flags al_test_support ${arg_SUPPORT} ${arg_LIBRARIES})
  target_include_directories(${target} PRIVATE ${arg_INCLUDES} ${INDRA_SOURCE_DIR}/test)
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
    list(APPEND arguments --isa-level ${level})
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
    # The main makes Catch2's "all skipped" CTest's 125, so a skip is not
    # left to the discovery's own code for it.
    catch_discover_tests(
      ${target}
      TEST_PREFIX "${target}/"
      TEST_LIST ${target}_cases
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
    # The library path is DL_PATHS', where AL_TEST_ENVIRONMENT has nothing
    # more to say.
    set(case_script "${CMAKE_CURRENT_BINARY_DIR}/${target}_cases.cmake")
    set(case_properties "LABELS [==[${labels}]==]")
    if(environment)
      string(APPEND case_properties " ENVIRONMENT [==[${environment}]==]")
    endif()
    if(AL_TEST_ENVIRONMENT)
      string(APPEND case_properties " ENVIRONMENT_MODIFICATION [==[${AL_TEST_ENVIRONMENT}]==]")
    endif()
    if(arg_RESOURCE_LOCK)
      string(APPEND case_properties " RESOURCE_LOCK [==[${arg_RESOURCE_LOCK}]==]")
    endif()
    file(
      WRITE "${case_script}"
      "foreach(case IN LISTS ${target}_cases)\n"
      "  set_tests_properties(\"\${case}\" PROPERTIES ${case_properties})\n"
      "endforeach()\n"
    )
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${case_script}")
  else()
    foreach(stem IN LISTS stems)
      add_test(
        NAME ${target}/${stem}
        COMMAND $<TARGET_FILE:${target}> --filenames-as-tags "[#${stem}]~[.]" ${arguments}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      )
      set_tests_properties(
        ${target}/${stem}
        PROPERTIES
          ENVIRONMENT "${environment}"
          ENVIRONMENT_MODIFICATION "${AL_TEST_ENVIRONMENT}"
          LABELS "${labels}"
          DISABLED ${disabled}
          SKIP_RETURN_CODE 125
          TIMEOUT ${timeout}
      )
      if(arg_RESOURCE_LOCK)
        set_tests_properties(${target}/${stem} PROPERTIES RESOURCE_LOCK "${arg_RESOURCE_LOCK}")
      endif()
    endforeach()

    list(LENGTH stems file_count)
    math(EXPR combined_timeout "${timeout} * ${file_count}")
    set(combined_label "combined_pending;manual")
    if(arg_COMBINED)
      set(combined_label combined)
    endif()
    add_test(
      NAME ${target}/combined
      COMMAND $<TARGET_FILE:${target}> ${arguments}
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

  add_dependencies(BUILD_TESTS ${target})
endfunction()
