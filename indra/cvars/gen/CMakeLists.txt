# -*- cmake -*-

# alcvargen, the settings schema's generator: reads indra/cvars/*.toml,
# checks every declaration, and writes the headers, descriptor tables and
# schema files that alcvar compiles. ALCVarGen.cmake runs it.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Built on its own at configure time, for the machine the build runs on,
  # so that the schema's headers are written before the first build. Its
  # inputs come from the viewer's configure: the TOML parser's headers, used
  # header-only, and the source tree for the schema's type headers.
  cmake_minimum_required(VERSION 4.0)
  project(alcvargen LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  foreach(var ALCVARGEN_INDRA ALCVARGEN_TOML_INCLUDE)
    if(NOT DEFINED ${var})
      message(FATAL_ERROR "alcvargen: ${var} is required")
    endif()
  endforeach()

  add_library(alcvargen_core STATIC alcvargen.cpp alcvargenjson.cpp)
  target_include_directories(
    alcvargen_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${ALCVARGEN_INDRA}/alcvar ${ALCVARGEN_INDRA}/llcommon
  )
  target_include_directories(alcvargen_core SYSTEM PRIVATE ${ALCVARGEN_TOML_INCLUDE})
  target_compile_definitions(alcvargen_core PRIVATE TOML_HEADER_ONLY=1)
  if(MSVC)
    target_compile_options(alcvargen_core PRIVATE /utf-8 /EHsc)
  endif()

  add_executable(alcvargen main.cpp)
  target_link_libraries(alcvargen PRIVATE alcvargen_core)
  set_target_properties(alcvargen PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(
      alcvargen
      PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR}/bin
    )
  endforeach()
  return()
endif()

add_library(alcvargen_core STATIC alcvargen.cpp alcvargenjson.cpp)
target_include_directories(
  alcvargen_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${INDRA_SOURCE_DIR}/alcvar ${INDRA_SOURCE_DIR}/llcommon
)
target_link_libraries(alcvargen_core PRIVATE al::flags PUBLIC ll::tomlplusplus)
set_target_properties(alcvargen_core PROPERTIES FOLDER "Tools")

add_executable(alcvargen main.cpp)
target_link_libraries(alcvargen PRIVATE al::flags alcvargen_core)
set_target_properties(alcvargen PROPERTIES FOLDER "Tools")

if(AL_BUILD_TESTS)
  # The JSON Schema editors check the declarations by, committed beside them.
  al_add_test_binary(
    alcvargen_tests
    PROJECT alcvargen
    SOURCES tests/alcvargen_test.cpp
    LIBRARIES alcvargen_core
    DEFINES "ALCVARGEN_COMMITTED_SCHEMA=\"${INDRA_SOURCE_DIR}/cvars/cvars.schema.json\""
  )
  # The generator's own build, for the machine the build runs on.
  add_test(
    NAME alcvargen_host
    COMMAND
      ${CMAKE_COMMAND} -DINDRA_SOURCE_DIR=${INDRA_SOURCE_DIR} -P
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/alcvargen_host_test.cmake
  )
  set_tests_properties(alcvargen_host PROPERTIES LABELS alcvargen)
  # The lock HEAD holds, which the lock's target folds the steps since by.
  add_test(
    NAME alcvargen_committed
    COMMAND
      ${CMAKE_COMMAND} -DINDRA_SOURCE_DIR=${INDRA_SOURCE_DIR}
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/committed_test -P
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/alcvargen_committed_test.cmake
  )
  set_tests_properties(alcvargen_committed PROPERTIES LABELS alcvargen SKIP_RETURN_CODE 77)
endif()
