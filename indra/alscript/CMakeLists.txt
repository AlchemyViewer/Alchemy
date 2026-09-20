# -*- cmake -*-
#
# The script analyzers: Luau's front end from Second Life's fork for SLua,
# Tailslide for LSL. Language-neutral and free of UI; what the studio's
# language services are built on (doc/SCRIPT_STUDIO.md, section 4.1).

add_library(alscript STATIC)

target_sources(alscript PRIVATE alluauservice.cpp allslservice.cpp alscriptenvelope.cpp)

target_sources(
  alscript
  PUBLIC
    FILE_SET HEADERS
      FILES alluauservice.h allslservice.h alscriptenvelope.h alscriptproblem.h alscriptsymbol.h
)

target_link_libraries(alscript PRIVATE al::flags ll::slua ll::tailslide PUBLIC llcommon)
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
  al_add_test(alscriptenvelope PROJECT alscript LIBRARIES alscript llcommon)
endif()
