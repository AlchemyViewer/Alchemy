# -*- cmake -*-
#
# Run by CPack after the install into its staging directory, before the
# archive is written: strips debug information from the Release binaries.
# `strip -S` keeps the symbol table, so a crash log still names functions.
# On Linux that is every ELF file under bin/ and lib/; on macOS the viewer
# executable. The symbols target keeps what is stripped, from the build
# tree's copies (ViewerSymbols.cmake). The install signed the
# bundle and stripping breaks the seal, so the bundle is sealed again.

if(NOT CPACK_BUILD_CONFIG STREQUAL "Release")
  return()
endif()
# The source package has nothing to strip.
if(CPACK_PACKAGE_FILE_NAME STREQUAL CPACK_SOURCE_PACKAGE_FILE_NAME)
  return()
endif()

find_program(al_strip strip REQUIRED)

function(al_strip_file path)
  execute_process(COMMAND "${al_strip}" -S "${path}" RESULT_VARIABLE result)
  if(result)
    message(FATAL_ERROR "strip failed (${result}) on ${path}")
  endif()
endfunction()

if(APPLE)
  file(GLOB bundles LIST_DIRECTORIES true "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*.app")
  foreach(bundle IN LISTS bundles)
    file(GLOB executables "${bundle}/Contents/MacOS/*")
    foreach(path IN LISTS executables)
      al_strip_file("${path}")
    endforeach()
    set(AL_SIGN_BUNDLE "${bundle}")
    set(AL_SIGN_IDENTITY "${CPACK_AL_SIGN_IDENTITY}")
    set(AL_SIGN_PLUGIN_ENTITLEMENTS "${CPACK_AL_SIGN_PLUGIN_ENTITLEMENTS}")
    set(AL_SIGN_HELPER_ENTITLEMENTS "${CPACK_AL_SIGN_HELPER_ENTITLEMENTS}")
    set(AL_SIGN_BUNDLE_ONLY ON)
    include("${CMAKE_CURRENT_LIST_DIR}/ViewerCodeSign.cmake")
  endforeach()
else()
  file(
    GLOB_RECURSE candidates
    LIST_DIRECTORIES false
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/bin/*"
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/lib/*"
  )
  foreach(path IN LISTS candidates)
    if(IS_SYMLINK "${path}")
      continue()
    endif()
    file(READ "${path}" magic LIMIT 4 HEX)
    if(magic STREQUAL "7f454c46")
      al_strip_file("${path}")
    endif()
  endforeach()
endif()
