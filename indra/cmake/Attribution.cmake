# -*- cmake -*-
#
# Writes the third-party attribution the viewer ships, from what vcpkg
# installed: packages-info.txt, which the About floater shows, and
# licenses.txt, every licence text in one file. Run as a script:
#
#   cmake -DVCPKG_STATUS=<vcpkg/status> -DVCPKG_SHARE=<triplet share dir>
#         -DTRIPLET=<triplet> -DTABLE=<attribution.json> -DSOURCE_DIR=<indra>
#         -DCHANNEL=<channel> -DVERSION=<version> -DENABLED=<opt,opt,...>
#         -DOUTPUT_INFO=<packages-info.txt> -DOUTPUT_LICENSES=<licenses.txt>
#         -P Attribution.cmake
#
# Each installed port's share/<port>/vcpkg.spdx.json gives its name, version,
# licence expression and homepage; share/<port>/copyright is the licence
# text, and the first line of it that opens with a copyright statement is
# the holder shown in the About floater. attribution.json fills in what
# vcpkg cannot know -- the viewer's own notice, the pieces under externals/,
# the SDKs from outside vcpkg, and overrides where a port's data is missing
# or its copyright file has no usable holder line. A shipped port with no
# licence in either place, or no copyright file, stops the build.

cmake_minimum_required(VERSION 4.0)

foreach(var VCPKG_STATUS VCPKG_SHARE TRIPLET TABLE SOURCE_DIR CHANNEL VERSION OUTPUT_INFO OUTPUT_LICENSES)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "Attribution.cmake: ${var} is not set")
  endif()
endforeach()
string(REPLACE "," ";" ENABLED "${ENABLED}")

# Build tools that vcpkg installs into the target triplet and are not shipped.
set(excluded pkgconf boost-uninstall)

file(READ "${TABLE}" table)

# ---------------------------------------------------------------------------
# The installed ports of the target triplet, from vcpkg's status file. Each
# stanza is one package or one feature of it; a package is listed once.

file(READ "${VCPKG_STATUS}" status)
string(REPLACE ";" "\\;" status "${status}")
string(REGEX REPLACE "\n\n+" ";" stanzas "${status}")
set(ports "")
foreach(stanza IN LISTS stanzas)
  if(stanza MATCHES "(^|\n)Architecture: ${TRIPLET}(\n|$)"
     AND stanza MATCHES "(^|\n)Status: install ok installed(\n|$)"
     AND stanza MATCHES "(^|\n)Package: ([^\n]+)")
    list(APPEND ports "${CMAKE_MATCH_2}")
  endif()
endforeach()
list(REMOVE_DUPLICATES ports)
list(SORT ports)
if(NOT ports)
  message(FATAL_ERROR "No installed ports for ${TRIPLET} in ${VCPKG_STATUS}")
endif()

# ---------------------------------------------------------------------------
# One record per shipped component, in the order they are written.

set(records "")
set(problems "")

# al_holder_line(<copyright file> <out>): the first line that opens with a
# copyright statement and a year, comment leaders and an SPDX tag allowed.
function(al_holder_line copyright out)
  file(STRINGS "${copyright}" lines ENCODING UTF-8 LIMIT_COUNT 1
    REGEX "^[ \t/*#-]*(SPDX-FileCopyrightText:[ \t]*)?([Cc]opyright|COPYRIGHT|\\(c\\)|\\(C\\)|©)[ \t]*(\\(c\\)|\\(C\\)|©)?[ \t]*[0-9][0-9][0-9][0-9]")
  set(line "")
  if(lines)
    list(GET lines 0 line)
    string(REGEX REPLACE "^[ \t/*#-]*(SPDX-FileCopyrightText:[ \t]*)?" "" line "${line}")
    string(REGEX REPLACE "<br>[ \t]*$" "" line "${line}")
    string(STRIP "${line}" line)
  endif()
  set(${out} "${line}" PARENT_SCOPE)
endfunction()

# al_table_get(<object json> <key> <out>): a string member, or empty.
function(al_table_get object key out)
  string(JSON type ERROR_VARIABLE error TYPE "${object}" "${key}")
  if(error OR NOT type STREQUAL "STRING")
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  string(JSON value GET "${object}" "${key}")
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

# al_add_record(<name> <version> <holder> <license> <homepage> <text>):
# appends one component, its fields in variables named by its identifier.
# The text is the licence text itself.
function(al_add_record name version holder license homepage text)
  string(MAKE_C_IDENTIFIER "${name}" id)
  foreach(field name version holder license homepage text)
    set(AL_${id}_${field} "${${field}}" PARENT_SCOPE)
  endforeach()
  list(APPEND records "${id}")
  set(records "${records}" PARENT_SCOPE)
endfunction()

# The ports. Boost is ninety ports under one licence; it is one entry.
set(boost_seen OFF)
string(JSON overrides ERROR_VARIABLE error GET "${table}" overrides)
if(error)
  set(overrides "{}")
endif()

foreach(port IN LISTS ports)
  if(port IN_LIST excluded OR port MATCHES "^vcpkg-")
    continue()
  endif()
  set(name "${port}")
  if(port MATCHES "^boost(-|$)")
    if(boost_seen)
      continue()
    endif()
    set(boost_seen ON)
    set(name boost)
    set(port boost-headers)
  endif()

  set(spdx "${VCPKG_SHARE}/${port}/vcpkg.spdx.json")
  set(copyright "${VCPKG_SHARE}/${port}/copyright")
  if(NOT EXISTS "${spdx}")
    list(APPEND problems "${name}: no vcpkg.spdx.json in ${VCPKG_SHARE}/${port}")
    continue()
  endif()
  file(READ "${spdx}" document)
  string(JSON package GET "${document}" packages 0)
  al_table_get("${package}" versionInfo version)
  al_table_get("${package}" licenseConcluded license)
  al_table_get("${package}" homepage homepage)
  string(REGEX REPLACE "#[0-9]+$" "" version "${version}")

  set(holder "")
  set(text "")
  if(EXISTS "${copyright}")
    al_holder_line("${copyright}" holder)
    file(READ "${copyright}" text)
  endif()

  # The table's word wins over the port's.
  string(JSON override ERROR_VARIABLE error GET "${overrides}" "${name}")
  if(NOT error)
    foreach(field holder license homepage)
      al_table_get("${override}" ${field} value)
      if(value)
        set(${field} "${value}")
      endif()
    endforeach()
    al_table_get("${override}" text text_file)
    if(text_file)
      file(READ "${SOURCE_DIR}/${text_file}" text)
    endif()
  endif()

  if(NOT license OR license MATCHES "^(NOASSERTION|LicenseRef-vcpkg-null)$")
    list(APPEND problems "${name}: no licence declared by the port and no override in ${TABLE}")
  endif()
  if(NOT text)
    list(APPEND problems "${name}: no copyright file at ${copyright} and no text in ${TABLE}")
  endif()
  if(NOT holder)
    message(WARNING "${name}: no copyright holder line found in ${copyright}; add a holder to ${TABLE}")
  endif()
  al_add_record("${name}" "${version}" "${holder}" "${license}" "${homepage}" "${text}")
endforeach()

# What vcpkg does not know about, gated on the option each entry names.
string(JSON extras ERROR_VARIABLE error GET "${table}" extras)
if(NOT error)
  string(JSON count LENGTH "${extras}")
  math(EXPR last "${count} - 1")
  foreach(i RANGE ${last})
    string(JSON extra GET "${extras}" ${i})
    al_table_get("${extra}" when when)
    if(when AND NOT when IN_LIST ENABLED)
      continue()
    endif()
    foreach(field name version holder license homepage notice)
      al_table_get("${extra}" ${field} ${field})
    endforeach()
    al_table_get("${extra}" text text_file)
    set(text "${notice}")
    if(text_file)
      file(READ "${SOURCE_DIR}/${text_file}" text)
    endif()
    if(NOT name OR NOT license OR NOT text)
      list(APPEND problems "extras[${i}]: name, license and text or notice are required")
      continue()
    endif()
    al_add_record("${name}" "${version}" "${holder}" "${license}" "${homepage}" "${text}")
  endforeach()
endif()

if(problems)
  list(JOIN problems "\n  " problems)
  message(FATAL_ERROR "Attribution is incomplete:\n  ${problems}")
endif()

# ---------------------------------------------------------------------------
# The viewer's own notice heads both files.

string(JSON viewer GET "${table}" viewer)
al_table_get("${viewer}" holder viewer_holder)
al_table_get("${viewer}" license viewer_license)
al_table_get("${viewer}" text viewer_text_file)
file(READ "${SOURCE_DIR}/${viewer_text_file}" viewer_text)

# Ports and extras together, by name.
list(SORT records COMPARE STRING CASE INSENSITIVE)

set(info "${CHANNEL} ${VERSION}\n${viewer_holder}\nLicense: ${viewer_license}\n\n")
set(licenses "==== ${CHANNEL} ${VERSION} ====\n\n${viewer_text}\n\n")
foreach(id IN LISTS records)
  foreach(field name version holder license homepage text)
    set(${field} "${AL_${id}_${field}}")
  endforeach()
  set(title "${name}")
  set(line "${name}")
  if(version)
    string(APPEND title " ${version}")
    string(APPEND line ": ${version}")
  endif()

  string(APPEND info "${line}\n")
  if(holder)
    string(APPEND info "${holder}\n")
  endif()
  string(APPEND info "License: ${license}\n")
  if(homepage)
    string(APPEND info "Source: ${homepage}\n")
  endif()
  string(APPEND info "\n")

  string(APPEND licenses "==== ${title} ====\n\n${text}\n\n")
endforeach()

file(WRITE "${OUTPUT_INFO}" "${info}")
file(WRITE "${OUTPUT_LICENSES}" "${licenses}")
list(LENGTH records count)
message(STATUS "Attributed ${count} components to ${OUTPUT_INFO}")
