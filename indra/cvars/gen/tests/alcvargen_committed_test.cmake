# -*- cmake -*-
#
# The lock HEAD holds, as the lock's target reads it to fold the steps since:
# HEAD's lines and nothing after them, an empty lock where HEAD has none, and
# nothing at all with no git, no commit, or a merge under way.
#
#   cmake -DINDRA_SOURCE_DIR=<indra> -DWORK_DIR=<scratch> -P alcvargen_committed_test.cmake

cmake_minimum_required(VERSION 4.0)

find_program(GIT git)
if(NOT GIT)
  message(STATUS "No git to read a commit with")
  cmake_language(EXIT 77)
endif()

set(script "${INDRA_SOURCE_DIR}/cmake/ALCVarCommittedLock.cmake")
set(repository "${WORK_DIR}/repository")
set(out "${WORK_DIR}/committed.lock")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${repository}")
set(failures "")

function(git)
  execute_process(
    COMMAND "${GIT}" ${ARGN}
    WORKING_DIRECTORY "${repository}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "git ${ARGN}: ${error}")
  endif()
endfunction()

# committed(<what> <git> <lock> <expected text, or ABSENT>)
function(committed what git lock expected)
  file(WRITE "${out}" "stale")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} "-DGIT=${git}" "-DREPOSITORY=${repository}" "-DLOCK=${lock}" "-DOUT=${out}"
      -P "${script}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    set(failures "${failures}\n  ${what}: the script failed: ${error}" PARENT_SCOPE)
    return()
  endif()
  if(expected STREQUAL "ABSENT")
    if(EXISTS "${out}")
      set(failures "${failures}\n  ${what}: wrote a lock" PARENT_SCOPE)
    endif()
    return()
  endif()
  if(NOT EXISTS "${out}")
    set(failures "${failures}\n  ${what}: wrote nothing" PARENT_SCOPE)
    return()
  endif()
  file(READ "${out}" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  if(NOT text STREQUAL expected)
    set(failures "${failures}\n  ${what}: '${text}', not '${expected}'" PARENT_SCOPE)
  endif()
endfunction()

git(init -q)
git(config user.email "test@alchemyviewer.org")
git(config user.name "alcvargen test")
git(config core.autocrlf false)
file(MAKE_DIRECTORY "${repository}/indra/cvars")
file(WRITE "${repository}/indra/cvars/cvars.lock" "first\n")
committed("no commit yet" "${GIT}" indra/cvars/cvars.lock ABSENT)

git(add indra/cvars/cvars.lock)
git(commit -q -m "a lock")
file(WRITE "${repository}/indra/cvars/cvars.lock" "first\nsecond\n")
committed("HEAD's lines and not the working tree's" "${GIT}" indra/cvars/cvars.lock "first\n")
committed("a lock HEAD does not hold" "${GIT}" indra/cvars/other.lock "")
committed("no git" "" indra/cvars/cvars.lock ABSENT)

execute_process(
  COMMAND "${GIT}" rev-parse HEAD
  WORKING_DIRECTORY "${repository}"
  OUTPUT_VARIABLE head
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
file(WRITE "${repository}/.git/MERGE_HEAD" "${head}\n")
committed("a merge under way" "${GIT}" indra/cvars/cvars.lock ABSENT)
file(REMOVE "${repository}/.git/MERGE_HEAD")
committed("the merge done" "${GIT}" indra/cvars/cvars.lock "first\n")

if(failures)
  message(FATAL_ERROR "The lock HEAD holds is not read as the lock's target needs it:${failures}")
endif()
message(STATUS "The lock HEAD holds is read as the lock's target needs it")
