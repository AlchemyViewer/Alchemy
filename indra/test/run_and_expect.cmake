# -*- cmake -*-
#
# Runs a test binary and checks what it did: its exit code, and what its output
# must and must not hold. For the Catch2 harness's own tests, whose exit codes
# and messages are its interface to CTest, which can check one of the two at a
# time.
#
#   cmake -DEXE=<binary> -DARGS=<arg|arg...> -DEXPECT_EXIT=<code>
#         [-DEXPECT_OUTPUT=<regex>] [-DREJECT_OUTPUT=<regex>] -P run_and_expect.cmake
#
# ARGS separates its arguments with |, since CTest would split them on ;.
# EXPECT_EXIT nonzero takes any end but a clean exit, a crash included, whose
# code a signal decides on one platform and the system on another.

string(REPLACE "|" ";" arguments "${ARGS}")
execute_process(
  COMMAND ${EXE} ${arguments}
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output
)
message("${output}")

if(EXPECT_EXIT STREQUAL "nonzero")
  if(exit_code STREQUAL "0")
    message(FATAL_ERROR "Expected the run to fail, and it exited 0")
  endif()
elseif(NOT exit_code STREQUAL EXPECT_EXIT)
  message(FATAL_ERROR "Expected exit code ${EXPECT_EXIT}, got ${exit_code}")
endif()
if(DEFINED EXPECT_OUTPUT AND NOT output MATCHES "${EXPECT_OUTPUT}")
  message(FATAL_ERROR "The output does not hold: ${EXPECT_OUTPUT}")
endif()
if(DEFINED REJECT_OUTPUT AND output MATCHES "${REJECT_OUTPUT}")
  message(FATAL_ERROR "The output holds what it must not: ${REJECT_OUTPUT}")
endif()
