# -*- cmake -*-
#
# Signs the installed macOS bundle inside out, run from the install rules
# with AL_SIGN_BUNDLE, AL_SIGN_IDENTITY, AL_SIGN_PLUGIN_ENTITLEMENTS and
# AL_SIGN_HELPER_ENTITLEMENTS set.
#
# Every nested piece is signed explicitly, deepest first: --deep would re-sign
# the helpers with the outer bundle's identity and entitlements and strip the
# sandbox and JIT entitlements the CEF helpers need. The identity is the
# Developer ID when one is configured, otherwise ad-hoc so a local build runs,
# sandbox helpers included, without a certificate.

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
message(STATUS "Signing ${AL_SIGN_BUNDLE} (${identity})")

function(al_codesign path)
  set(entitlements "")
  if(ARGC GREATER 1 AND EXISTS "${ARGV1}")
    set(entitlements --entitlements "${ARGV1}")
  endif()
  execute_process(
    COMMAND codesign --force --sign "${identity}" --options runtime ${timestamp} ${entitlements} "${path}"
    RESULT_VARIABLE result)
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

# 1. Loose Mach-O files anywhere in the bundle, so every enclosing bundle
#    seals over already-signed code.
file(GLOB_RECURSE loose LIST_DIRECTORIES false
  "${contents}/*.dylib" "${contents}/*.so" "${contents}/*.bin" "${contents}/*.dat")
foreach(path IN LISTS loose)
  al_codesign("${path}")
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

# 4. The viewer itself, sealing everything above. The plugin entitlements
#    carry disable-library-validation, which the hardened runtime needs to
#    load our own dylibs.
al_codesign("${AL_SIGN_BUNDLE}" "${AL_SIGN_PLUGIN_ENTITLEMENTS}")
