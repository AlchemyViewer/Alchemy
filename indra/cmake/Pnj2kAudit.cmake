# -*- cmake -*-
#
# Fails where pnj2k's static library gives away how the codec is written: a
# path into its sources, which a panic's location or a line table holds, or
# a symbol that spells out a path through its modules. Run as a script once
# the library is built:
#
#   cmake -DLIBRARY=<the static library> -DSTAMP=<a file to write on success>
#         -P Pnj2kAudit.cmake
#
# The Rust standard library's own paths and names, which the library also
# holds, are public and pass.

cmake_minimum_required(VERSION 4.0)

foreach(var LIBRARY STAMP)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "Pnj2kAudit.cmake: ${var} is not set")
  endif()
endforeach()
if(NOT EXISTS "${LIBRARY}")
  message(FATAL_ERROR "Pnj2kAudit.cmake: ${LIBRARY} does not exist")
endif()

# A source file of either crate, in a checkout or in cargo's copy of one.
file(STRINGS "${LIBRARY}" paths REGEX "crates[/\\]pnj2k(-capi)?[/\\]src[/\\]" LIMIT_COUNT 5)

# A symbol mangled with its path: the crate's name, then a module's or an
# item's, each behind its length, as `..._5pnj2k5tier2...`. A hashed symbol
# has the crate's name alone, `_RNxC5pnj2k11H<hash>`.
file(STRINGS "${LIBRARY}" names REGEX "(5pnj2k|10pnj2k_capi)[0-9]+[a-z_]" LIMIT_COUNT 5)

set(found "")
foreach(path IN LISTS paths)
  string(APPEND found "\n  a source path: ${path}")
endforeach()
foreach(name IN LISTS names)
  string(APPEND found "\n  a symbol's path: ${name}")
endforeach()
if(found)
  message(
    FATAL_ERROR
    "${LIBRARY} gives away pnj2k's sources:${found}\nIt must be built with no debug information, no panic locations and hashed symbols; see cmake/Pnj2k.cmake."
  )
endif()

file(WRITE "${STAMP}" "")
