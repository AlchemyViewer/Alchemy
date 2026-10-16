# -*- cmake -*-
#
# Run by CPack after the install into its staging directory, for the .deb
# and the .rpm: lays out what they install outside the tree.
#
#   /usr/bin/<package>   the launcher, linked to /opt/<package>/alchemy
#   /usr/share/...       the desktop entry, its AppStream data and its icons,
#                        copied from the tree's share/; the tree keeps its
#                        copy, so the launcher finds the entry installed
#
# The tree loses install.sh, which installs a tree from the archive, and CEF's
# sandbox helper is made setuid; the package owns it as root. The .rpm is
# told the names of the libraries the tree carries, so that what links them
# does not require them of the system.
#
# Listed in CPACK_PRE_BUILD_SCRIPTS by ViewerPackageLinux.cmake.

if(NOT (CPACK_GENERATOR STREQUAL "DEB" OR CPACK_GENERATOR STREQUAL "RPM"))
  return()
endif()

set(al_root "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
set(al_prefix "${CPACK_PACKAGING_INSTALL_PREFIX}")
set(al_tree "${al_root}${al_prefix}")
if(NOT EXISTS "${al_tree}/bin/alchemy-bin")
  message(FATAL_ERROR "No viewer in the staged tree at ${al_tree}")
endif()

foreach(dir applications metainfo icons)
  file(COPY "${al_tree}/share/${dir}" DESTINATION "${al_root}/usr/share")
endforeach()
file(MAKE_DIRECTORY "${al_root}/usr/bin")
file(CREATE_LINK "${al_prefix}/alchemy" "${al_root}/usr/bin/${CPACK_AL_LINUX_PACKAGE}" SYMBOLIC)

file(REMOVE "${al_tree}/install.sh")

set(al_sandbox "${al_tree}/bin/llplugin/chrome-sandbox")
if(EXISTS "${al_sandbox}")
  file(
    CHMOD
    "${al_sandbox}"
    PERMISSIONS
      OWNER_READ
      OWNER_WRITE
      OWNER_EXECUTE
      GROUP_READ
      GROUP_EXECUTE
      WORLD_READ
      WORLD_EXECUTE
      SETUID
  )
endif()

# rpm's dependency generator matches a requirement such as
# libcef.so()(64bit); a bracketed character needs no escaping in the spec.
if(CPACK_GENERATOR STREQUAL "RPM")
  file(GLOB_RECURSE al_libraries LIST_DIRECTORIES false "${al_tree}/*.so" "${al_tree}/*.so.*")
  set(al_names "")
  foreach(path IN LISTS al_libraries)
    get_filename_component(name "${path}" NAME)
    string(REGEX REPLACE "([.+])" "[\\1]" name "${name}")
    list(APPEND al_names "${name}")
  endforeach()
  list(REMOVE_DUPLICATES al_names)
  if(al_names)
    list(JOIN al_names "|" al_names)
    string(APPEND CPACK_RPM_SPEC_MORE_DEFINE "\n%global __requires_exclude ^(${al_names})[(]")
  endif()
endif()
