# -*- cmake -*-
include_guard()
add_library(ll::sentry INTERFACE IMPORTED)

if(AL_USE_SENTRY)
  if(WINDOWS OR LINUX)
    find_package(sentry CONFIG REQUIRED)
    target_link_libraries(ll::sentry INTERFACE sentry::sentry)
  endif()

  if(NOT AL_SENTRY_DSN)
    message(
      FATAL_ERROR
      "AL_USE_SENTRY needs the DSN of the project to report to: -DAL_SENTRY_DSN=<url>"
    )
  endif()

  target_compile_definitions(ll::sentry INTERFACE AL_SENTRY=1 AL_SENTRY_DSN="${AL_SENTRY_DSN}")
endif()
