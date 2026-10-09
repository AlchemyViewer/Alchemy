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

string(REPLACE "|" ";" arguments "${ARGS}")
execute_process(
  COMMAND ${EXE} ${arguments}
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output
)
message("${output}")

if(NOT exit_code STREQUAL EXPECT_EXIT)
  message(FATAL_ERROR "Expected exit code ${EXPECT_EXIT}, got ${exit_code}")
endif()
if(DEFINED EXPECT_OUTPUT AND NOT output MATCHES "${EXPECT_OUTPUT}")
  message(FATAL_ERROR "The output does not hold: ${EXPECT_OUTPUT}")
endif()
if(DEFINED REJECT_OUTPUT AND output MATCHES "${REJECT_OUTPUT}")
  message(FATAL_ERROR "The output holds what it must not: ${REJECT_OUTPUT}")
endif()
