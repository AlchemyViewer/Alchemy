# -*- cmake -*-
#
# Fetches the master message template and runs template_verifier against
# ours. Run as a script from cmake/TemplateCheck.cmake:
#
#   cmake -DTOOL=<template_verifier> -DTEMPLATE=<message_template.msg>
#         -DMASTER_URL=<url> -DMASTER_CACHE=<file> -DMODE=<development|production>
#         -DSTAMP=<file> -P VerifyTemplate.cmake
#
# The master is kept in MASTER_CACHE for four hours between fetches, always
# refetched in production mode. When it cannot be fetched and there is no
# cached copy, development mode checks the template's syntax alone;
# production mode fails.

cmake_minimum_required(VERSION 4.0)

foreach(var TOOL TEMPLATE MASTER_URL MASTER_CACHE MODE STAMP)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "VerifyTemplate.cmake: ${var} is not set")
  endif()
endforeach()

set(max_age 14400)
set(refresh ON)
if(EXISTS "${MASTER_CACHE}" AND NOT MODE STREQUAL "production")
  file(TIMESTAMP "${MASTER_CACHE}" cached "%s" UTC)
  string(TIMESTAMP now "%s" UTC)
  math(EXPR age "${now} - ${cached}")
  if(age LESS max_age)
    set(refresh OFF)
  endif()
endif()

if(refresh)
  message(STATUS "Fetching the master message template from ${MASTER_URL}")
  file(DOWNLOAD "${MASTER_URL}" "${MASTER_CACHE}.download" STATUS status TIMEOUT 60)
  list(GET status 0 code)
  if(code EQUAL 0)
    file(RENAME "${MASTER_CACHE}.download" "${MASTER_CACHE}")
  else()
    list(GET status 1 reason)
    file(REMOVE "${MASTER_CACHE}.download")
    if(MODE STREQUAL "production")
      message(FATAL_ERROR "Cannot fetch the master message template: ${reason}")
    elseif(EXISTS "${MASTER_CACHE}")
      message(WARNING "Cannot fetch the master message template (${reason}); comparing against the cached copy")
    else()
      message(WARNING "Cannot fetch the master message template (${reason}); checking the template's syntax only")
    endif()
  endif()
endif()

set(master "")
if(EXISTS "${MASTER_CACHE}")
  set(master "${MASTER_CACHE}")
endif()

execute_process(
  COMMAND "${TOOL}" --mode ${MODE} "${TEMPLATE}" ${master}
  RESULT_VARIABLE result)
if(result)
  message(FATAL_ERROR "The message template check failed (${result})")
endif()
file(TOUCH "${STAMP}")
