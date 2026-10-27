# -*- cmake -*-
#
# The settings lock as the last commit holds it, for the lock's target to fold
# the steps a setting gained since into one:
#
#   cmake -DGIT=<git> -DREPOSITORY=<dir> -DLOCK=<path in it> -DOUT=<file>
#         -P ALCVarCommittedLock.cmake
#
# OUT goes first, and is written only where git can say what HEAD holds. With
# no git or no commit, or with a merge, rebase, cherry-pick or revert under
# way, whose other side's lines HEAD does not hold yet, there is nothing to
# fold by, and the generator records each change as a step of its own. A lock
# HEAD does not hold yet is an empty one.

cmake_minimum_required(VERSION 4.0)

file(REMOVE "${OUT}")
if(NOT GIT OR NOT EXISTS "${GIT}")
  return()
endif()

function(_al_git out_result)
  execute_process(
    COMMAND "${GIT}" ${ARGN}
    WORKING_DIRECTORY "${REPOSITORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(${out_result} "${result}" PARENT_SCOPE)
  set(git_output "${output}" PARENT_SCOPE)
endfunction()

_al_git(result rev-parse --verify -q HEAD)
if(NOT result EQUAL 0)
  return()
endif()
foreach(head MERGE_HEAD REBASE_HEAD CHERRY_PICK_HEAD REVERT_HEAD)
  _al_git(result rev-parse --verify -q ${head})
  if(result EQUAL 0)
    message(STATUS "A ${head} is under way: the lock's new steps are each recorded, not folded")
    return()
  endif()
endforeach()
foreach(rebase rebase-merge rebase-apply)
  _al_git(result rev-parse --git-path ${rebase})
  if(NOT IS_ABSOLUTE "${git_output}")
    set(git_output "${REPOSITORY}/${git_output}")
  endif()
  if(result EQUAL 0 AND IS_DIRECTORY "${git_output}")
    message(STATUS "A rebase is under way: the lock's new steps are each recorded, not folded")
    return()
  endif()
endforeach()

_al_git(result cat-file -e "HEAD:${LOCK}")
if(NOT result EQUAL 0)
  file(WRITE "${OUT}" "")
  return()
endif()
execute_process(
  COMMAND "${GIT}" show "HEAD:${LOCK}"
  WORKING_DIRECTORY "${REPOSITORY}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${OUT}"
  ERROR_QUIET
)
if(NOT result EQUAL 0)
  file(REMOVE "${OUT}")
endif()
