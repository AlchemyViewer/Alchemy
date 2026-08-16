# -*- cmake -*-

include_guard()

if(NOT BUILD_TESTING)
  return()
endif()

find_package(GTest CONFIG REQUIRED)

add_library(ll::gtest ALIAS GTest::gtest)
add_library(ll::gtest_main ALIAS GTest::gtest_main)
add_library(ll::gmock ALIAS GTest::gmock)
add_library(ll::gmock_main ALIAS GTest::gmock_main)

include(GoogleTest)
include(LLTestCommand)

function(_RD_GOOGLETEST_SOURCE_FILES OUTVAR)
  set(test_sources)

  foreach(source IN LISTS ARGN)
    get_filename_component(source_name "${source}" NAME_WE)
    get_filename_component(source_extension "${source}" EXT)
    get_filename_component(source_directory "${source}" DIRECTORY)

    if(source_directory)
      set(test_source
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/${source_directory}/${source_name}_test${source_extension}")
    else()
      set(test_source
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/${source_name}_test${source_extension}")
    endif()

    if(NOT EXISTS "${test_source}")
      message(FATAL_ERROR
        "RD GoogleTest helpers expected test source ${test_source} for ${source}")
    endif()

    list(APPEND test_sources "${test_source}")
  endforeach()

  set(${OUTVAR} "${test_sources}" PARENT_SCOPE)
endfunction()

function(_RD_ADD_GOOGLETEST_TARGET target sources link_targets)
  add_executable(${target} ${sources})
  target_link_libraries(${target}
    PRIVATE
    ll::gtest_main
    ll::gmock
    ${link_targets}
  )
  target_include_directories(${target}
    PRIVATE
    ${INDRA_SOURCE_DIR}/test
  )

  set_target_properties(${target}
    PROPERTIES
    FOLDER "Tests/GoogleTest"
    RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
  )

  if(WINDOWS)
    target_link_options(${target}
      PRIVATE
      $<$<CONFIG:Release>:/DEBUG:NONE>
    )
  elseif(DARWIN)
    set_target_properties(${target}
      PROPERTIES
      BUILD_WITH_INSTALL_RPATH 1
      INSTALL_RPATH "@executable_path/Frameworks"
      XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )
  elseif(LINUX)
    set_property(TARGET ${target} APPEND PROPERTY
      BUILD_RPATH "${SHARED_LIB_STAGING_DIR}")
  endif()

  if(TARGET stage_third_party_libs)
    add_dependencies(${target} stage_third_party_libs)
  endif()

  if(TARGET BUILD_TESTS)
    add_dependencies(BUILD_TESTS ${target})
  endif()

  LL_TEST_LIBRARY_PATH(test_library_path)
  LL_TEST_LAUNCHER(test_launcher "${test_library_path}")

  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    set_property(TARGET ${target} PROPERTY
      TEST_LAUNCHER "${test_launcher}")
  elseif(NOT CMAKE_CROSSCOMPILING)
    # CMake 3.27-3.28 do not support TEST_LAUNCHER. The older
    # CROSSCOMPILING_EMULATOR property is still consumed by the GoogleTest
    # module on the native builds supported by this project.
    set_property(TARGET ${target} PROPERTY
      CROSSCOMPILING_EMULATOR "${test_launcher}")
  endif()

  set(runtime_environment_variable PATH)
  if(DARWIN OR LINUX)
    set(runtime_environment_variable LD_LIBRARY_PATH)
  endif()

  set(runtime_environment_modification
    "${runtime_environment_variable}=path_list_prepend:$<TARGET_FILE_DIR:${target}>")
  foreach(path IN LISTS test_library_path)
    string(APPEND runtime_environment_modification
      ";${runtime_environment_variable}=path_list_prepend:${path}")
  endforeach()

  gtest_discover_tests(${target}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PROPERTIES
    ENVIRONMENT_MODIFICATION "${runtime_environment_modification}"
  )
endfunction()

function(RD_ADD_UNIT_TESTS project sources)
  if(ARGC LESS 2)
    message(FATAL_ERROR "RD_ADD_UNIT_TESTS requires a project and source list")
  endif()

  if(NOT project)
    message(FATAL_ERROR "RD_ADD_UNIT_TESTS requires a project")
  endif()

  # An empty source list is a valid no-op while a module is being migrated.
  if(NOT sources)
    return()
  endif()

  _RD_GOOGLETEST_SOURCE_FILES(test_sources ${sources})

  set(test_target "${project}_tests")
  _RD_ADD_GOOGLETEST_TARGET("${test_target}" "${test_sources}" "${project}")
endfunction()

function(RD_ADD_INTEGRATION_TEST
    testname
    additional_source_files
    library_dependencies
    test_project
  )
  if(ARGC LESS 4)
    message(FATAL_ERROR
      "RD_ADD_INTEGRATION_TEST requires a test name, source files, "
      "library dependencies, and project")
  endif()

  if(NOT testname)
    message(FATAL_ERROR "RD_ADD_INTEGRATION_TEST requires a test name")
  endif()

  if(NOT test_project)
    message(FATAL_ERROR "RD_ADD_INTEGRATION_TEST requires a project")
  endif()

  set(test_source
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/${testname}_test.cpp"
  )
  if(NOT EXISTS "${test_source}")
    message(FATAL_ERROR
      "RD_ADD_INTEGRATION_TEST expected test source ${test_source}")
  endif()

  set(test_sources "${test_source}")
  list(APPEND test_sources ${additional_source_files})

  set(test_target "${test_project}_${testname}_integration_test")
  _RD_ADD_GOOGLETEST_TARGET(
    "${test_target}"
    "${test_sources}"
    "${library_dependencies}"
  )

  set_target_properties("${test_target}"
    PROPERTIES
    FOLDER "Tests/GoogleTest/${test_project}"
  )
endfunction()
