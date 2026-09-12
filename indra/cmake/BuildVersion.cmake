# -*- cmake -*-

# Construct the viewer version number based on the indra/VIEWER_VERSION file
if (NOT DEFINED VIEWER_SHORT_VERSION) # will be true in indra/, false in indra/newview/
    set(VIEWER_VERSION_BASE_FILE "${INDRA_SOURCE_DIR}/newview/VIEWER_VERSION.txt")

    if ( EXISTS ${VIEWER_VERSION_BASE_FILE} )
        file(STRINGS ${VIEWER_VERSION_BASE_FILE} VIEWER_SHORT_VERSION REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+")
        string(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+" "\\1" VIEWER_VERSION_MAJOR ${VIEWER_SHORT_VERSION})
        string(REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+" "\\1" VIEWER_VERSION_MINOR ${VIEWER_SHORT_VERSION})
        string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" VIEWER_VERSION_PATCH ${VIEWER_SHORT_VERSION})

        # VIEWER_VERSION_REVISION_SOURCE names where the number came from;
        # the configuration report prints it beside the version.
        if (DEFINED ENV{revision})
           set(VIEWER_VERSION_REVISION $ENV{revision})
           set(VIEWER_VERSION_REVISION_SOURCE "environment")

        elseif (DEFINED ENV{GITHUB_RUN_ID})
           set(VIEWER_VERSION_REVISION $ENV{GITHUB_RUN_ID})
           set(VIEWER_VERSION_REVISION_SOURCE "GitHub run id")

        else ()
            find_package(Git)
            if (Git_FOUND)
                execute_process(
                        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
                        OUTPUT_VARIABLE VIEWER_VERSION_REVISION
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                if ("${VIEWER_VERSION_REVISION}" MATCHES "^[0-9]+$")
                    set(VIEWER_VERSION_REVISION_SOURCE "git")
                else ()
                    set(VIEWER_VERSION_REVISION 0)
                    set(VIEWER_VERSION_REVISION_SOURCE "none: not a git repository")
                endif ()
            else ()
                set(VIEWER_VERSION_REVISION 0)
                set(VIEWER_VERSION_REVISION_SOURCE "none: git not found")
            endif ()
        endif ()
    else ()
        message(SEND_ERROR "Cannot get viewer version from '${VIEWER_VERSION_BASE_FILE}'")
    endif ()

    if ("${VIEWER_VERSION_REVISION}" STREQUAL "")
      set(VIEWER_VERSION_REVISION 0)
      set(VIEWER_VERSION_REVISION_SOURCE "none: revision was blank")
    endif ()
endif ()
