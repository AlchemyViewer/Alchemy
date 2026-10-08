# -*- cmake -*-
#
# Lays out an AppDir around an installed Linux tree, for vpk to make the
# AppImage of:
#
#   cmake -DAL_APPDIR=<dir>.AppDir -P ViewerAppDir.cmake
#
# The tree is installed at <dir>.AppDir/usr/bin beforehand, as it is: the
# viewer's Velopack client finds its updater and manifest in the usr/bin of
# the path it runs from, which vpk puts there, and so the tree keeps its own
# layout below. Added around it:
#
#   AppRun                 runs the tree's launcher
#   <app id>.desktop       the tree's desktop entry, running the launcher
#                          by its name, as AppImage tools expect
#   <app id>.png, .DirIcon the icon
#   usr/share/             the icons and the AppStream data, where the tools
#                          that add an AppImage to the desktop look
#
# The tree loses install.sh, which installs a tree from the archive.
#
# Run by the velopack target (ViewerPackage.cmake) and by the hosted build's
# Linux packaging job, from the archive's tree.

if(NOT AL_APPDIR MATCHES "\\.AppDir$")
  message(FATAL_ERROR "AL_APPDIR must name a directory ending in .AppDir, not '${AL_APPDIR}'")
endif()
set(al_tree "${AL_APPDIR}/usr/bin")
if(NOT EXISTS "${al_tree}/bin/alchemy-bin" OR NOT EXISTS "${al_tree}/alchemy")
  message(FATAL_ERROR "No installed viewer at ${al_tree}")
endif()

file(GLOB al_entries "${al_tree}/share/applications/*.desktop")
list(LENGTH al_entries al_entry_count)
if(NOT al_entry_count EQUAL 1)
  message(FATAL_ERROR "Expected one desktop entry under ${al_tree}/share/applications")
endif()
# The ID is the name less .desktop; it has dots of its own.
get_filename_component(al_app_id "${al_entries}" NAME)
string(REGEX REPLACE "\\.desktop$" "" al_app_id "${al_app_id}")

file(REMOVE "${al_tree}/install.sh")

file(READ "${al_entries}" al_entry)
string(REGEX REPLACE "\nExec=[^\n]*" "\nExec=alchemy %u" al_entry "${al_entry}")
file(WRITE "${AL_APPDIR}/${al_app_id}.desktop" "${al_entry}")

set(al_icon "${al_tree}/share/icons/hicolor/256x256/apps/${al_app_id}.png")
file(COPY_FILE "${al_icon}" "${AL_APPDIR}/${al_app_id}.png")
file(COPY_FILE "${al_icon}" "${AL_APPDIR}/.DirIcon")

file(MAKE_DIRECTORY "${AL_APPDIR}/usr/share")
foreach(dir icons metainfo)
  file(COPY "${al_tree}/share/${dir}" DESTINATION "${AL_APPDIR}/usr/share")
endforeach()

file(
  WRITE "${AL_APPDIR}/AppRun"
  "#!/bin/sh
# The AppImage's entry: the viewer's launcher, in the tree at usr/bin.
HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"
exec \"$HERE/usr/bin/alchemy\" \"$@\"
"
)
file(
  CHMOD
  "${AL_APPDIR}/AppRun"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
