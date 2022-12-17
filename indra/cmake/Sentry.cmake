include(Variables)

#Crash reporting
option(USE_SENTRY "Use the Sentry crash reporting system" OFF)
if (DEFINED ENV{USE_SENTRY})
  set(USE_SENTRY $ENV{USE_SENTRY} CACHE BOOL "" FORCE)
endif()

if(DEFINED ENV{SENTRY_DSN})
    set(SENTRY_DSN $ENV{SENTRY_DSN} CACHE STRING "Sentry DSN" FORCE)
endif()

if (INSTALL_PROPRIETARY)
    # Note that viewer_manifest.py makes decision based on SENTRY_DSN and not USE_SENTRY
    if (SENTRY_DSN)
        set(USE_SENTRY ON  CACHE BOOL "Use the Sentry crash reporting system" FORCE)
    else (SENTRY_DSN)
        set(USE_SENTRY OFF CACHE BOOL "Use the Sentry crash reporting system" FORCE)
    endif (SENTRY_DSN)
else (INSTALL_PROPRIETARY)
    set(USE_SENTRY OFF CACHE BOOL "Use the Sentry crash reporting system" FORCE)
endif (INSTALL_PROPRIETARY)

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
            include(ZLIBNG)
            set(SENTRY_LIBRARIES 
                ${ARCH_PREBUILT_DIRS_RELEASE}/libsentry.a
                ${ARCH_PREBUILT_DIRS_RELEASE}/libbreakpad_client.a
                ${CURL_LIBRARIES}
                ${NGHTTP2_LIBRARIES}
                ${OPENSSL_LIBRARIES}
                ${ZLIBNG_LIBRARIES})
        endif ()
    else ()
        find_package(Sentry REQUIRED)
    endif ()

    if(SENTRY_DSN STREQUAL "")
        message(FATAL_ERROR "You must set a DSN url with -DSENTRY_DSN= to enable sentry")
    endif()

    set(SENTRY_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/sentry)
    set(SENTRY_DEFINE "USE_SENTRY=1")
endif ()
