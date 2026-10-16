# -*- cmake -*-

# Construct the viewer version number based on the indra/VIEWER_VERSION file
if(NOT DEFINED VIEWER_SHORT_VERSION) # will be true in indra/, false in indra/newview/
  set(VIEWER_VERSION_BASE_FILE "${INDRA_SOURCE_DIR}/newview/VIEWER_VERSION.txt")

  if(EXISTS ${VIEWER_VERSION_BASE_FILE})
    file(STRINGS ${VIEWER_VERSION_BASE_FILE} VIEWER_SHORT_VERSION REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+")
    string(
      REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+"
      "\\1"
      VIEWER_VERSION_MAJOR
      ${VIEWER_SHORT_VERSION}
    )
    string(
      REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+"
      "\\1"
      VIEWER_VERSION_MINOR
      ${VIEWER_SHORT_VERSION}
    )
    string(
      REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+)"
      "\\1"
      VIEWER_VERSION_PATCH
      ${VIEWER_SHORT_VERSION}
    )

    # VIEWER_VERSION_REVISION_SOURCE names where the number came from;
    # the configuration report prints it beside the version.
    if(DEFINED ENV{revision})
      set(VIEWER_VERSION_REVISION $ENV{revision})
      set(VIEWER_VERSION_REVISION_SOURCE "environment")

    elseif(DEFINED ENV{GITHUB_RUN_ID})
      set(VIEWER_VERSION_REVISION $ENV{GITHUB_RUN_ID})
      set(VIEWER_VERSION_REVISION_SOURCE "GitHub run id")

    else()
      find_package(Git)
      if(Git_FOUND)
        # The count is a walk of the whole history, so it is kept
        # against the commit it was taken for. The files git moves
        # HEAD through are configure dependencies, so a new commit
        # or a checkout reconfigures instead of shipping the
        # previous count.
        execute_process(
          COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
          WORKING_DIRECTORY ${INDRA_SOURCE_DIR}
          OUTPUT_VARIABLE git_head
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET
        )
        if(git_head MATCHES "^[0-9a-f]+$")
          execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --git-path HEAD --git-path logs/HEAD
            WORKING_DIRECTORY ${INDRA_SOURCE_DIR}
            OUTPUT_VARIABLE git_head_files
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
          )
          string(REPLACE "\n" ";" git_head_files "${git_head_files}")
          foreach(head_file IN LISTS git_head_files)
            if(EXISTS "${INDRA_SOURCE_DIR}/${head_file}")
              set_property(
                DIRECTORY
                APPEND
                PROPERTY CMAKE_CONFIGURE_DEPENDS "${INDRA_SOURCE_DIR}/${head_file}"
              )
            elseif(EXISTS "${head_file}")
              set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${head_file}")
            endif()
          endforeach()

          if(NOT AL_GIT_REVISION_COMMIT STREQUAL git_head)
            execute_process(
              COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
              WORKING_DIRECTORY ${INDRA_SOURCE_DIR}
              OUTPUT_VARIABLE git_count
              OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(
              AL_GIT_REVISION_COMMIT
              "${git_head}"
              CACHE INTERNAL
              "The commit AL_GIT_REVISION_COUNT was taken for"
            )
            set(
              AL_GIT_REVISION_COUNT
              "${git_count}"
              CACHE INTERNAL
              "Number of commits reachable from AL_GIT_REVISION_COMMIT"
            )
          endif()
          set(VIEWER_VERSION_REVISION ${AL_GIT_REVISION_COUNT})
          set(VIEWER_VERSION_REVISION_SOURCE "git")
        else()
          set(VIEWER_VERSION_REVISION 0)
          set(VIEWER_VERSION_REVISION_SOURCE "none: not a git repository")
        endif()
      else()
        set(VIEWER_VERSION_REVISION 0)
        set(VIEWER_VERSION_REVISION_SOURCE "none: git not found")
      endif()
    endif()
  else()
    message(SEND_ERROR "Cannot get viewer version from '${VIEWER_VERSION_BASE_FILE}'")
  endif()

  if("${VIEWER_VERSION_REVISION}" STREQUAL "")
    set(VIEWER_VERSION_REVISION 0)
    set(VIEWER_VERSION_REVISION_SOURCE "none: revision was blank")
  endif()
endif()
