# -*- cmake -*-

# The settings store: each setting's value in a typed slot that any thread
# reads with one load, the layers the value is worked out from, and the
# schema alcvargen compiles from indra/cvars, whose handles every library
# above reads.

set(alcvar_SOURCES alcvars.cpp alcvarvalue.cpp)

set(
  alcvar_HEADERS
  alcvar.h
  alcvaratomic.h
  alcvarconnection.h
  alcvarconvertible.h
  alcvardesc.h
  alcvars.h
  alcvartype.h
  alcvarvalue.h
)

add_library(alcvar STATIC)
target_sources(alcvar PRIVATE ${alcvar_SOURCES} PUBLIC FILE_SET HEADERS FILES ${alcvar_HEADERS})
target_link_libraries(alcvar PRIVATE al::flags PUBLIC llcommon llmath)
target_precompile_headers(alcvar REUSE_FROM llprecompiled)

include(ALCVarGen)

# The viewer's schema, one file an area.
file(GLOB cvars_schema CONFIGURE_DEPENDS "${INDRA_SOURCE_DIR}/cvars/*.toml")
al_generate_cvars(
  alcvar
  ROOT cvar
  INCLUDE_DIR cvars
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
  LOCK "${INDRA_SOURCE_DIR}/cvars/cvars.lock"
  LOCK_TARGET cvars_lock
  SCHEMA ${cvars_schema}
)

if(AL_BUILD_TESTS)
  # The tests' own schema, under cvartest::, so that they run against
  # settings of their own rather than the viewer's.
  add_library(alcvar_test_schema OBJECT)
  target_link_libraries(alcvar_test_schema PRIVATE al::flags PUBLIC alcvar)
  set_target_properties(alcvar_test_schema PROPERTIES FOLDER "Tests/alcvar")
  al_generate_cvars(
    alcvar_test_schema
    ROOT cvartest
    INCLUDE_DIR cvartest
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_schema"
    LOCK "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/cvars.lock"
    SCHEMA
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/render.toml
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/ui.toml
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema/chat.toml
  )

  al_add_test_binary(
    alcvar_tests
    PROJECT alcvar
    SOURCES tests/alcvarschema_test.cpp tests/alcvarlayers_test.cpp
    LIBRARIES alcvar
    SUPPORT alcvar_test_schema llcommon_test_checks
  )
endif()
