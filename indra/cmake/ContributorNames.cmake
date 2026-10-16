# -*- cmake -*-
include_guard()

# al_contributor_names(<contributions.txt> <output>)
#
# Writes the contributor names from doc/contributions.txt as one
# comma-separated line, the form the About floater reads. The file's header
# runs to its first blank line; after that a line starting in column one is a
# name and an indented line is an issue it contributed to.
function(al_contributor_names contributions output)
  file(STRINGS "${contributions}" lines ENCODING UTF-8)
  set(in_header ON)
  set(names "")
  foreach(line IN LISTS lines)
    if(in_header)
      if(line MATCHES "^[ \t]*$")
        set(in_header OFF)
      endif()
    elseif(line MATCHES "^[^ \t]")
      string(REGEX REPLACE "[ \t]+$" "" line "${line}")
      list(APPEND names "${line}")
    endif()
  endforeach()
  list(JOIN names ", " names)
  file(CONFIGURE OUTPUT "${output}" CONTENT "${names}" @ONLY)
endfunction()
