# -*- cmake -*-
#
# Run by CPack after the install into its staging directory, before the
# archive is written: strips debug information from the Release binaries.
# `strip -S` keeps the symbol table, so a crash log still names functions.
# On Linux that is every ELF file under bin/ and lib/; on macOS the viewer
# executable, whose dSYM is generated separately.

if(NOT CPACK_BUILD_CONFIG STREQUAL "Release")
  return()
endif()

find_program(al_strip strip REQUIRED)

function(al_strip_file path)
  execute_process(COMMAND "${al_strip}" -S "${path}" RESULT_VARIABLE result)
  if(result)
    message(FATAL_ERROR "strip failed (${result}) on ${path}")
  endif()
endfunction()

if(APPLE)
  file(GLOB executables "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*.app/Contents/MacOS/*")
  foreach(path IN LISTS executables)
    al_strip_file("${path}")
  endforeach()
else()
  file(
    GLOB_RECURSE candidates
    LIST_DIRECTORIES false
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/bin/*"
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/lib/*"
  )
  foreach(path IN LISTS candidates)
    if(IS_SYMLINK "${path}")
      continue()
    endif()
    file(READ "${path}" magic LIMIT 4 HEX)
    if(magic STREQUAL "7f454c46")
      al_strip_file("${path}")
    endif()
  endforeach()
endif()
