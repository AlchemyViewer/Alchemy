include(Variables)

if (USE_SENTRY)
    if (NOT USESYSTEMLIBS)
        include(Prebuilt)
        use_prebuilt_binary(sentry)
        if (WINDOWS)
            set(SENTRY_LIBRARIES ${ARCH_PREBUILT_DIRS_RELEASE}/sentry.lib)
        elseif (DARWIN)
            find_library(SENTRY_LIBRARIES Sentry REQUIRED
                NO_DEFAULT_PATH PATHS "${ARCH_PREBUILT_DIRS_RELEASE}")
        else ()
            include(CURL)
            include(NGHTTP2)
            include(OpenSSL)
            include(ZLIB)
            set(SENTRY_LIBRARIES 
                ${ARCH_PREBUILT_DIRS_RELEASE}/libsentry.a
                ${ARCH_PREBUILT_DIRS_RELEASE}/libbreakpad_client.a
                ${CURL_LIBRARIES}
                ${NGHTTP2_LIBRARIES}
                ${OPENSSL_LIBRARIES}
                ${ZLIB_LIBRARIES})
        endif ()
    else ()
        find_package(Sentry REQUIRED)
    endif ()

    if(DEFINED ENV{SENTRY_DSN})
        set(SENTRY_DSN $ENV{SENTRY_DSN} CACHE STRING "Sentry DSN" FORCE)
    else()
        set(SENTRY_DSN "" CACHE STRING "Sentry DSN")
    endif()

    if(SENTRY_DSN STREQUAL "")
        message(FATAL_ERROR "You must set a DSN url with -DSENTRY_DSN= to enable sentry")
    endif()

    set(SENTRY_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/sentry)
    set(SENTRY_DEFINE "USE_SENTRY=1")
endif ()
