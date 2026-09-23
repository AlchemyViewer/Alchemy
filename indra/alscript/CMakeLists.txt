# -*- cmake -*-
#
# The script analyzers: Luau's front end from Second Life's fork for SLua,
# Tailslide for LSL. Language-neutral and free of UI; what the studio's
# language services are built on.

add_library(alscript STATIC)

target_sources(
  alscript
  PRIVATE
    aldiskincludes.cpp
    allslinliner.cpp
    almessagemap.cpp
    allsloptimizer.cpp
    alluauconfig.cpp
    alluauservice.cpp
    allslservice.cpp
    allsltraits.cpp
    alpreprocessor.cpp
    alscriptenvelope.cpp
    alscriptformatter.cpp
    alscriptstack.cpp
    alsourcemap.cpp
)

target_sources(
  alscript
  PUBLIC
    FILE_SET HEADERS
      FILES
        aldiskincludes.h
        allslinliner.h
        almessagemap.h
        allsloptimizer.h
        allsltraits.inc
        alluauconfig.h
        alluauservice.h
        allslservice.h
        allsltraits.h
        alpreprocessor.h
        alscriptengine.h
        alscriptenvelope.h
        alscriptformatter.h
        alscriptproblem.h
        alscriptstack.h
        alscriptsymbol.h
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
  al_add_test(
    alluauservice
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  al_add_test(
    allslservice
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines}
  )
  al_add_test(aldiskincludes PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptstack PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptenvelope PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(almessagemap PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alluauconfig PROJECT alscript LIBRARIES alscript llcommon)
  al_add_test(alscriptformatter PROJECT alscript LIBRARIES alscript llcommon)
  # The optimizer's golden files under tests/optimizer/, checked with the
  # grid's builtins loaded.
  al_add_test(
    allsloptimizer
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
  # The golden files under tests/preprocessor/: every test_*.lsl beside
  # its expected output.
  al_add_test(
    alpreprocessor
    PROJECT alscript
    LIBRARIES alscript llcommon
    DEFINES ${definitions_defines} "AL_ALSCRIPT_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
  )
endif()
