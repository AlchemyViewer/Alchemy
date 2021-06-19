# -*- cmake -*-

option(REVISION_FROM_VCS "Get current revision from vcs" ON)
# Construct the viewer channel from environment variables or defaults
if(NOT DEFINED VIEWER_CHANNEL)
    if(DEFINED ENV{VIEWER_CHANNEL_BASE})
        set(VIEWER_CHANNEL_BASE
            $ENV{VIEWER_CHANNEL_BASE}
            CACHE STRING "Viewer Channel Base Name" FORCE)
    else()
        set(VIEWER_CHANNEL_BASE
            "Alchemy"
            CACHE STRING "Viewer Channel Base Name")
    endif()

    if(DEFINED ENV{VIEWER_CHANNEL_TYPE})
        set(VIEWER_CHANNEL_TYPE
            $ENV{VIEWER_CHANNEL_TYPE}
            CACHE STRING "Viewer Channel Type Name" FORCE)
    else()
        set(VIEWER_CHANNEL_TYPE
            "Test"
            CACHE STRING "Viewer Channel Type Name")
    endif()

    if(DEFINED ENV{VIEWER_CHANNEL_CODENAME})
        set(VIEWER_CHANNEL_CODENAME_INTERNAL $ENV{VIEWER_CHANNEL_CODENAME})
    elseif(DEFINED VIEWER_CHANNEL_CODENAME)
        set(VIEWER_CHANNEL_CODENAME_INTERNAL ${VIEWER_CHANNEL_CODENAME})
    else()
        set(VIEWER_CHANNEL_CODENAME_FILE "${CMAKE_SOURCE_DIR}/newview/VIEWER_PROJECT_CODENAME.txt")

        if(EXISTS ${VIEWER_CHANNEL_CODENAME_FILE})
            file(STRINGS ${VIEWER_CHANNEL_CODENAME_FILE} VIEWER_CHANNEL_CODENAME_INTERNAL)
        else()
            set(VIEWER_CHANNEL_CODENAME_INTERNAL "Default")
        endif()
    endif()
    if("${VIEWER_CHANNEL_TYPE}" STREQUAL "Project")
        set(VIEWER_CHANNEL "${VIEWER_CHANNEL_BASE} ${VIEWER_CHANNEL_TYPE} ${VIEWER_CHANNEL_CODENAME_INTERNAL}")
    else()
        set(VIEWER_CHANNEL "${VIEWER_CHANNEL_BASE} ${VIEWER_CHANNEL_TYPE}")
    endif()
endif()

# Construct the viewer version number based on the indra/VIEWER_VERSION file
if(NOT DEFINED VIEWER_SHORT_VERSION) # will be true in indra/, false in indra/newview/
    set(VIEWER_VERSION_BASE_FILE "${CMAKE_SOURCE_DIR}/newview/VIEWER_VERSION.txt")

    if(EXISTS ${VIEWER_VERSION_BASE_FILE})
        file(STRINGS ${VIEWER_VERSION_BASE_FILE} VIEWER_SHORT_VERSION REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+")
        string(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+" "\\1" VIEWER_VERSION_MAJOR ${VIEWER_SHORT_VERSION})
        string(REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+" "\\1" VIEWER_VERSION_MINOR ${VIEWER_SHORT_VERSION})
        string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" VIEWER_VERSION_PATCH ${VIEWER_SHORT_VERSION})

        if(REVISION_FROM_VCS)
            find_package(Git)
        endif()

        if((NOT REVISION_FROM_VCS) AND DEFINED ENV{CI_PIPELINE_ID})
            set(VIEWER_VERSION_REVISION $ENV{CI_PIPELINE_ID})
            message(STATUS "Revision (from environment): ${VIEWER_VERSION_REVISION}")
        elseif((NOT REVISION_FROM_VCS) AND DEFINED ENV{AUTOBUILD_BUILD_ID})
            set(VIEWER_VERSION_REVISION $ENV{AUTOBUILD_BUILD_ID})
            message(STATUS "Revision (from autobuild environment): ${VIEWER_VERSION_REVISION}")
        elseif(Git_FOUND)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count
                OUTPUT_VARIABLE GIT_REV_LIST_COUNT
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_STRIP_TRAILING_WHITESPACE)

            if(GIT_REV_LIST_COUNT)
                set(VIEWER_VERSION_REVISION ${GIT_REV_LIST_COUNT})
            else(GIT_REV_LIST_COUNT)
                set(VIEWER_VERSION_REVISION 0)
            endif(GIT_REV_LIST_COUNT)
        else()
            set(VIEWER_VERSION_REVISION 0)
        endif()
        message(STATUS "Building '${VIEWER_CHANNEL}' Version ${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
    else(EXISTS ${VIEWER_VERSION_BASE_FILE})
        message(SEND_ERROR "Cannot get viewer version from '${VIEWER_VERSION_BASE_FILE}'")
    endif(EXISTS ${VIEWER_VERSION_BASE_FILE})

    if("${VIEWER_VERSION_REVISION}" STREQUAL "")
        message(STATUS "Ultimate fallback, revision was blank or not set: will use 0")
        set(VIEWER_VERSION_REVISION 0)
    endif("${VIEWER_VERSION_REVISION}" STREQUAL "")
endif(NOT DEFINED VIEWER_SHORT_VERSION)

if (NOT DEFINED VIEWER_COMMIT_LONG_SHA)
    if((NOT REVISION_FROM_VCS) AND DEFINED ENV{CI_COMMIT_SHA})
        set(VIEWER_COMMIT_LONG_SHA $ENV{CI_COMMIT_SHA})
        message(STATUS "Long SHA (from environment): ${VIEWER_COMMIT_LONG_SHA}")
    elseif(Git_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
                        OUTPUT_VARIABLE GIT_COMMIT_SHA
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        )

        if(GIT_COMMIT_SHA)
            set(VIEWER_COMMIT_LONG_SHA ${GIT_COMMIT_SHA})
        else()
            set(VIEWER_COMMIT_LONG_SHA 0)
        endif()
        message(STATUS "Long SHA: ${VIEWER_COMMIT_LONG_SHA}")
    else()
        set(VIEWER_COMMIT_LONG_SHA 0)
    endif()
endif (NOT DEFINED VIEWER_COMMIT_LONG_SHA)

if (NOT DEFINED VIEWER_COMMIT_SHORT_SHA)
    if((NOT REVISION_FROM_VCS) AND DEFINED ENV{CI_COMMIT_SHORT_SHA})
        set(VIEWER_COMMIT_SHORT_SHA $ENV{CI_COMMIT_SHORT_SHA})
        message(STATUS "Short SHA (from environment): ${VIEWER_COMMIT_SHORT_SHA}")
    elseif(Git_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                        OUTPUT_VARIABLE GIT_COMMIT_SHORT_SHA
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        )

        if(GIT_COMMIT_SHORT_SHA)
            set(VIEWER_COMMIT_SHORT_SHA ${GIT_COMMIT_SHORT_SHA})
        else()
            set(VIEWER_COMMIT_SHORT_SHA 0)
        endif()
        message(STATUS "Short SHA: ${VIEWER_COMMIT_LONG_SHA}")
    else()
        set(VIEWER_COMMIT_SHORT_SHA 0)
    endif()
endif (NOT DEFINED VIEWER_COMMIT_SHORT_SHA)