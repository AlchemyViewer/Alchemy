# -*- cmake -*-

set(AL_TEMPLATE_VERIFIER_MODE "development" CACHE STRING "Strictness of the message template check: development accepts an older or mixed template, production only the same or a newer one")
set_property(CACHE AL_TEMPLATE_VERIFIER_MODE PROPERTY STRINGS development production)
set(AL_TEMPLATE_VERIFIER_MASTER_URL "https://github.com/secondlife/master-message-template/raw/master/message_template.msg" CACHE STRING "Location of the master message template")

# Verifies the message template against the master with template_verifier,
# as a step the target depends on. It runs when the template, the tool or
# the scripts change, not on every link of the target.
macro (check_message_template _target)
  set(message_template_stamp "${CMAKE_CURRENT_BINARY_DIR}/message_template.verified")
  add_custom_command(
      OUTPUT ${message_template_stamp}
      COMMAND ${CMAKE_COMMAND}
              -DTOOL=$<TARGET_FILE:template_verifier>
              -DTEMPLATE=${SCRIPTS_DIR}/messages/message_template.msg
              -DMASTER_URL=${AL_TEMPLATE_VERIFIER_MASTER_URL}
              -DMASTER_CACHE=${CMAKE_CURRENT_BINARY_DIR}/master_message_template.msg
              -DMODE=${AL_TEMPLATE_VERIFIER_MODE}
              -DSTAMP=${message_template_stamp}
              -P ${INDRA_SOURCE_DIR}/cmake/VerifyTemplate.cmake
      DEPENDS
          template_verifier
          ${SCRIPTS_DIR}/messages/message_template.msg
          ${INDRA_SOURCE_DIR}/cmake/VerifyTemplate.cmake
      COMMENT "Verifying message template - See http://wiki.secondlife.com/wiki/Template_verifier.py"
      VERBATIM
      )
  target_sources(${_target} PRIVATE ${message_template_stamp})
endmacro (check_message_template)
