# -*- cmake -*-

include(Python)

set(AL_TEMPLATE_VERIFIER_OPTIONS "" CACHE STRING "Options for scripts/template_verifier.py")
set(AL_TEMPLATE_VERIFIER_MASTER_URL "https://github.com/secondlife/master-message-template/raw/master/message_template.msg" CACHE STRING "Location of the master message template")

# Verifies the message template against the master, as a step the target
# depends on. It runs when the template or the verifier changes, not on
# every link of the target.
macro (check_message_template _target)
  set(message_template_stamp "${CMAKE_CURRENT_BINARY_DIR}/message_template.verified")
  separate_arguments(message_template_options NATIVE_COMMAND "${AL_TEMPLATE_VERIFIER_OPTIONS}")
  add_custom_command(
      OUTPUT ${message_template_stamp}
      COMMAND ${Python3_EXECUTABLE}
              ${SCRIPTS_DIR}/template_verifier.py
              --mode=development --cache_master --master_url=${AL_TEMPLATE_VERIFIER_MASTER_URL} ${message_template_options}
      COMMAND ${CMAKE_COMMAND} -E touch ${message_template_stamp}
      DEPENDS
          ${SCRIPTS_DIR}/messages/message_template.msg
          ${SCRIPTS_DIR}/template_verifier.py
      COMMENT "Verifying message template - See http://wiki.secondlife.com/wiki/Template_verifier.py"
      VERBATIM
      )
  target_sources(${_target} PRIVATE ${message_template_stamp})
endmacro (check_message_template)
