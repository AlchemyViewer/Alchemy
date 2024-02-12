# -*- cmake -*-
include(Linking)
include(Prebuilt)
include(CURL)
include(NGHTTP2)
include(OpenSSL)
include(ZLIBNG)

include_guard()
if (DEFINED ENV{USE_SENTRY})
  set(USE_SENTRY $ENV{USE_SENTRY} CACHE BOOL "" FORCE)
endif()

if(DEFINED ENV{SENTRY_DSN})
  set(SENTRY_DSN $ENV{SENTRY_DSN} CACHE STRING "Sentry DSN" FORCE)
else()
  set(SENTRY_DSN "" CACHE STRING "Sentry DSN")
endif()

if (INSTALL_PROPRIETARY AND NOT SENTRY_DSN STREQUAL "")
  set(USE_SENTRY ON CACHE BOOL "Use the Sentry crash reporting system")
endif ()

if (USE_SENTRY)
    add_library( al::sentry INTERFACE IMPORTED )
    use_prebuilt_binary(sentry)
    if (WINDOWS)
        target_link_libraries( al::sentry INTERFACE ${ARCH_PREBUILT_DIRS_RELEASE}/sentry.lib)
    elseif (DARWIN)
        find_library(SENTRY_LIBRARIES Sentry REQUIRED
            NO_DEFAULT_PATH PATHS "${ARCH_PREBUILT_DIRS_RELEASE}")
        target_link_libraries( al::sentry INTERFACE ${SENTRY_LIBRARIES})
    else ()
        target_link_libraries( al::sentry INTERFACE
            ${ARCH_PREBUILT_DIRS_RELEASE}/libsentry.a
            ${ARCH_PREBUILT_DIRS_RELEASE}/libbreakpad_client.a
            ll::libcurl
            ll::openssl
            ll::nghttp2
            ll::zlib-ng
        )
    endif ()
    target_include_directories( al::sentry SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/sentry)

    if(SENTRY_DSN STREQUAL "")
        message(FATAL_ERROR "You must set a DSN url with -DSENTRY_DSN= to enable sentry")
    endif()

    target_compile_definitions( al::sentry INTERFACE AL_SENTRY=1 SENTRY_DSN="${SENTRY_DSN}")
endif ()
