# -*- cmake -*-
include_guard()
add_library(ll::sentry INTERFACE IMPORTED)

# sentry-native on Windows and Linux, the Cocoa SDK's Objective-C framework on
# macOS. AL_SENTRY itself rides al_flags (00-Common.cmake) so every library
# sees it; the DSN is the reporter's alone.
if(AL_USE_SENTRY)
  if(DARWIN)
    find_package(unofficial-sentry-cocoa CONFIG REQUIRED)
    target_link_libraries(ll::sentry INTERFACE unofficial::sentry-cocoa::sentry)
  else()
    find_package(sentry CONFIG REQUIRED)
    target_link_libraries(ll::sentry INTERFACE sentry::sentry)
  endif()

  if(NOT AL_SENTRY_DSN)
    message(
      FATAL_ERROR
      "AL_USE_SENTRY needs the DSN of the project to report to: -DAL_SENTRY_DSN=<url>"
    )
  endif()

  target_compile_definitions(ll::sentry INTERFACE AL_SENTRY_DSN="${AL_SENTRY_DSN}")
endif()
