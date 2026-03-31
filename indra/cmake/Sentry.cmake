# -*- cmake -*-
include_guard()

if (USE_SENTRY)
    add_library( al::sentry INTERFACE IMPORTED )

    if (WINDOWS OR LINUX)
        find_package(sentry CONFIG REQUIRED)
        target_link_libraries( al::sentry INTERFACE sentry::sentry)
    elseif (DARWIN)
        # find_library(SENTRY_LIBRARIES Sentry REQUIRED
        #     NO_DEFAULT_PATH PATHS "${ARCH_PREBUILT_DIRS_RELEASE}")
        # target_link_libraries( al::sentry INTERFACE ${SENTRY_LIBRARIES})
    endif ()

    if(NOT DEFINED SENTRY_DSN OR SENTRY_DSN STREQUAL "")
        message(FATAL_ERROR "You must set a DSN url with -DSENTRY_DSN= to enable sentry")
    endif()

    target_compile_definitions( al::sentry INTERFACE AL_SENTRY=1 SENTRY_DSN="${SENTRY_DSN}")
endif ()
