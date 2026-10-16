# -*- cmake -*-
#
# Signs a macOS bundle inside out. Run from the install rules, from the
# package step after it strips the executable, and by the hosted build's
# macOS packaging job as `cmake -D... -P ViewerCodeSign.cmake`, with:
#
#   AL_SIGN_BUNDLE               the .app
#   AL_SIGN_IDENTITY             a Developer ID, or empty for ad-hoc
#   AL_SIGN_PLUGIN_ENTITLEMENTS  the viewer's and the plugin hosts'
#   AL_SIGN_HELPER_ENTITLEMENTS  the CEF helpers'
#   AL_SIGN_KEYCHAIN             optional, the keychain holding the identity
#   AL_SIGN_BUNDLE_ONLY          optional, re-seal the outer bundle alone: for
#                                when only the main executable has changed
#
# Every nested piece is signed explicitly, deepest first: --deep would re-sign
# the helpers with the outer bundle's identity and entitlements and strip the
# sandbox and JIT entitlements the CEF helpers need. The identity is the
# Developer ID when one is configured, otherwise ad-hoc so a local build runs,
# sandbox helpers included, without a certificate.

foreach(var AL_SIGN_BUNDLE AL_SIGN_PLUGIN_ENTITLEMENTS AL_SIGN_HELPER_ENTITLEMENTS)
  if("${${var}}" STREQUAL "")
    message(FATAL_ERROR "ViewerCodeSign.cmake needs ${var}")
  endif()
endforeach()
if(NOT IS_DIRECTORY "${AL_SIGN_BUNDLE}")
  message(FATAL_ERROR "No bundle to sign at ${AL_SIGN_BUNDLE}")
endif()

if(AL_SIGN_IDENTITY)
  set(identity "${AL_SIGN_IDENTITY}")
  set(timestamp --timestamp)
else()
  set(identity "-")
  set(timestamp "")
endif()
set(keychain "")
if(AL_SIGN_KEYCHAIN)
  set(keychain --keychain "${AL_SIGN_KEYCHAIN}")
endif()
message(STATUS "Signing ${AL_SIGN_BUNDLE} (${identity})")

function(al_codesign path)
  set(entitlements "")
  if(ARGC GREATER 1)
    if(NOT EXISTS "${ARGV1}")
      message(FATAL_ERROR "No entitlements at ${ARGV1}")
    endif()
    set(entitlements --entitlements "${ARGV1}")
  endif()
  execute_process(
    COMMAND
      codesign --force --sign "${identity}" ${keychain} --options runtime ${timestamp}
      ${entitlements} "${path}"
    RESULT_VARIABLE result
  )
  if(result)
    message(FATAL_ERROR "codesign failed (${result}) on ${path}")
  endif()
endfunction()

# Orders a list of paths deepest first.
function(al_deepest_first var)
  set(keyed "")
  foreach(path IN LISTS ${var})
    string(REGEX MATCHALL "/" slashes "${path}")
    list(LENGTH slashes depth)
    math(EXPR key "1000 - ${depth}")
    list(APPEND keyed "${key}|${path}")
  endforeach()
  list(SORT keyed)
  set(sorted "")
  foreach(item IN LISTS keyed)
    string(REGEX REPLACE "^[0-9]+\\|" "" path "${item}")
    list(APPEND sorted "${path}")
  endforeach()
  set(${var} "${sorted}" PARENT_SCOPE)
endfunction()

set(contents "${AL_SIGN_BUNDLE}/Contents")

if(NOT AL_SIGN_BUNDLE_ONLY)
  # 1. Loose Mach-O libraries anywhere in the bundle, so every enclosing
  #    bundle seals over already-signed code. Data files are sealed by their
  #    bundle's resource rules; a signature of their own would live in
  #    extended attributes, which archives and update packages drop.
  file(GLOB_RECURSE loose LIST_DIRECTORIES false "${contents}/*.dylib" "${contents}/*.so")
  foreach(path IN LISTS loose)
    if(NOT IS_SYMLINK "${path}")
      al_codesign("${path}")
    endif()
  endforeach()

  # 2. Frameworks, deepest first.
  file(GLOB_RECURSE frameworks LIST_DIRECTORIES true "${contents}/*.framework")
  list(FILTER frameworks INCLUDE REGEX "\\.framework$")
  al_deepest_first(frameworks)
  foreach(path IN LISTS frameworks)
    al_codesign("${path}")
  endforeach()

  # 3. Nested applications: the CEF helpers with their own entitlements, then
  #    the plugin hosts, deepest first.
  file(GLOB_RECURSE apps LIST_DIRECTORIES true "${contents}/*.app")
  list(FILTER apps INCLUDE REGEX "\\.app$")
  al_deepest_first(apps)
  foreach(path IN LISTS apps)
    get_filename_component(name "${path}" NAME)
    if(name MATCHES "^DullahanHelper")
      al_codesign("${path}" "${AL_SIGN_HELPER_ENTITLEMENTS}")
    else()
      al_codesign("${path}" "${AL_SIGN_PLUGIN_ENTITLEMENTS}")
    endif()
  endforeach()
endif()

# 4. The viewer itself, sealing everything above. The plugin entitlements
#    carry disable-library-validation, which the hardened runtime needs to
#    load our own dylibs.
al_codesign("${AL_SIGN_BUNDLE}" "${AL_SIGN_PLUGIN_ENTITLEMENTS}")
