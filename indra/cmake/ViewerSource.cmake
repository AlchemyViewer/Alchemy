# -*- cmake -*-
#
# Fills the source package's staging directory from git: the committed tree
# of the repository and of each submodule, at the commit checked out, so the
# package holds exactly what is tracked and nothing the build wrote into the
# source tree. Run by cpack, before it collects files, from
# CPACK_INSTALL_SCRIPTS; the source configuration is the one whose package
# name is the source package's, and for the binary configuration this does
# nothing.

if(NOT CPACK_PACKAGE_FILE_NAME STREQUAL CPACK_SOURCE_PACKAGE_FILE_NAME)
  return()
endif()

if(NOT CPACK_AL_GIT)
  message(FATAL_ERROR "The source package is taken from git, which the configure did not find")
endif()

function(al_git out)
  execute_process(
    COMMAND "${CPACK_AL_GIT}" -C "${CPACK_AL_REPOSITORY}" ${ARGN}
    OUTPUT_VARIABLE output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE result
  )
  if(result)
    string(JOIN " " args ${ARGN})
    message(FATAL_ERROR "git ${args} failed (${result}) in ${CPACK_AL_REPOSITORY}")
  endif()
  set(${out} "${output}" PARENT_SCOPE)
endfunction()

al_git(head rev-parse HEAD)
al_git(changes status --porcelain --untracked-files=no)
if(changes)
  message(
    WARNING
    "The source package is the committed tree at ${head}; the working tree has changes that are not in it"
  )
endif()

# Every submodule must be checked out, or the package would not build.
al_git(submodule_status submodule status --recursive)
string(REPLACE "\n" ";" submodule_lines "${submodule_status}")
set(submodules "")
foreach(line IN LISTS submodule_lines)
  if(line MATCHES "^-[0-9a-f]+ ([^ ]+)")
    message(
      FATAL_ERROR
      "The submodule ${CMAKE_MATCH_1} is not checked out: run 'git submodule update --init --recursive'"
    )
  elseif(line MATCHES "^[ +U][0-9a-f]+ ([^ ]+)")
    list(APPEND submodules "${CMAKE_MATCH_1}")
  endif()
endforeach()

# git archive takes one repository at a time and writes what the commit
# holds, with the commit's time on every entry; each is unpacked under its
# path in the tree.
set(archives "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/source-archives")
file(REMOVE_RECURSE "${archives}")
file(MAKE_DIRECTORY "${archives}" "${CMAKE_INSTALL_PREFIX}")
set(n 0)
foreach(repository IN ITEMS "" ${submodules})
  math(EXPR n "${n} + 1")
  set(archive "${archives}/${n}.tar")
  if(repository)
    set(directory "${CPACK_AL_REPOSITORY}/${repository}")
    set(prefix "--prefix=${repository}/")
  else()
    set(directory "${CPACK_AL_REPOSITORY}")
    set(prefix "")
  endif()
  message(STATUS "Archiving ${directory}")
  execute_process(
    COMMAND "${CPACK_AL_GIT}" -C "${directory}" archive --format=tar ${prefix} -o "${archive}" HEAD
    RESULT_VARIABLE result
  )
  if(result)
    message(FATAL_ERROR "git archive failed (${result}) in ${directory}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${CMAKE_INSTALL_PREFIX}")
endforeach()
file(REMOVE_RECURSE "${archives}")
