include(Python)

MACRO(LL_TEST_LIBRARY_PATH LISTVAR)
  IF(WINDOWS)
    # We typically build/package only Release variants of third-party
    # libraries, so append the Release staging dir in case the library being
    # sought doesn't have a debug variant.
    set(${LISTVAR} ${SHARED_LIB_STAGING_DIR} ${SHARED_LIB_STAGING_DIR}/Release)
  ELSEIF(DARWIN)
    # We typically build/package only Release variants of third-party
    # libraries, so append the Release staging dir in case the library being
    # sought doesn't have a debug variant.
    set(${LISTVAR} ${SHARED_LIB_STAGING_DIR} ${SHARED_LIB_STAGING_DIR}/Release/Frameworks /usr/lib)
  ELSEIF(LINUX)
    # Linux uses a single staging directory anyway.
    set(${LISTVAR} ${SHARED_LIB_STAGING_DIR} /usr/lib)
  ELSE()
    set(${LISTVAR})
  ENDIF()
ENDMACRO(LL_TEST_LIBRARY_PATH)

MACRO(LL_TEST_LAUNCHER OUTVAR LD_LIBRARY_PATH)
  SET(value
    ${Python3_EXECUTABLE}
    "${INDRA_SOURCE_DIR}/cmake/run_build_test.py")
  FOREACH(dir ${LD_LIBRARY_PATH})
    LIST(APPEND value "-l${dir}")
  ENDFOREACH(dir)
  LIST(APPEND value "-DPYTHON=${Python3_EXECUTABLE}")
  SET(${OUTVAR} ${value})
ENDMACRO(LL_TEST_LAUNCHER)

MACRO(LL_TEST_COMMAND OUTVAR LD_LIBRARY_PATH)
  # nat wonders how Kitware can use the term 'function' for a construct that
  # cannot return a value. And yet, variables you set inside a FUNCTION are
  # local. Try a MACRO instead.
  LL_TEST_LAUNCHER(value "${LD_LIBRARY_PATH}")
  # Enough different tests want to be able to find CMake's Python3_EXECUTABLE
  # that we should just pop it into the environment for everybody.
  LIST(APPEND value ${ARGN})
  SET(${OUTVAR} ${value})
##IF(LL_TEST_VERBOSE)
##  MESSAGE(STATUS "LL_TEST_COMMAND: ${value}")
##ENDIF(LL_TEST_VERBOSE)
ENDMACRO(LL_TEST_COMMAND)
