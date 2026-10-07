# -*- cmake -*-
#
# The .deb and the .rpm. Each holds the installed tree as it is, under
# /opt/<package>, with what the desktop needs of it outside: the launcher on
# the PATH as /usr/bin/<package>, and the entry, its AppStream data and its
# icons under /usr/share, from the tree's share/. CPack moves the prefix for
# these generators alone (ViewerPackageConfig.cmake), and lays out what lies
# outside it after the install (ViewerSystemPackage.cmake), so the install
# rules and the archive know nothing of either. CEF's sandbox helper is made
# setuid root there too, which sandboxes the web browser's renderers.
#
# Dependencies are found from what the binaries link, less the libraries the
# tree carries itself. What the viewer loads by name at run time is not
# linked and is listed here: EGL and OpenGL, which it cannot run without,
# and recommended, what one desktop or another needs of Wayland, X11, sound
# and input. NSS is listed too: dpkg-shlibdeps names only the libraries the
# building system has installed, and CEF links NSS where a build host may
# have none.
#
# Each channel is a package of its own, which installs beside the others.
# The package manager updates them; the viewer's own updater does not.
#
# Included from ViewerPackage.cmake on Linux, before CPack.
include_guard()

set(
  AL_PACKAGE_CONTACT
  "Alchemy Viewer Project <staff@alchemyviewer.org>"
  CACHE STRING
  "The maintainer the .deb and .rpm name"
)

set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/ViewerPackageConfig.cmake")
list(APPEND CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/ViewerSystemPackage.cmake")
set(CPACK_AL_LINUX_PACKAGE "${AL_LINUX_PACKAGE}")
set(CPACK_AL_APP_ID "${AL_APP_ID}")

set(CPACK_PACKAGE_CONTACT "${AL_PACKAGE_CONTACT}")
set(
  CPACK_PACKAGE_DESCRIPTION
  "Alchemy is a third-party viewer for Second Life and OpenSimulator grids,
built on Linden Lab's viewer with a modern renderer, post-processing, and a
script editor of its own."
)

set(CPACK_DEBIAN_PACKAGE_NAME "${AL_LINUX_PACKAGE}")
set(CPACK_DEBIAN_PACKAGE_SECTION games)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_COMPRESSION_TYPE zstd)
if(ARCH STREQUAL "arm64")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
else()
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
endif()
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libegl1, libopengl0, libnss3")
set(
  CPACK_DEBIAN_PACKAGE_RECOMMENDS
  "libwayland-client0, libwayland-cursor0, libwayland-egl1, libdecor-0-0, libxkbcommon0, libx11-6, libxext6, libxcursor1, libxfixes3, libxi6, libxrandr2, libxss1, libpulse0, libasound2t64 | libasound2, libudev1, xdg-utils"
)

set(CPACK_RPM_PACKAGE_NAME "${AL_LINUX_PACKAGE}")
set(CPACK_RPM_PACKAGE_LICENSE "LGPL-2.1-only")
set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_COMPRESSION_TYPE zstd)
if(ARCH STREQUAL "arm64")
  set(CPACK_RPM_PACKAGE_ARCHITECTURE aarch64)
else()
  set(CPACK_RPM_PACKAGE_ARCHITECTURE x86_64)
endif()
# Not relocatable: the launcher on the PATH links into /opt.
set(CPACK_PACKAGE_RELOCATABLE OFF)
set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
# Nothing the tree carries is offered to the rest of the system.
set(CPACK_RPM_PACKAGE_AUTOPROV OFF)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "libEGL.so.1()(64bit), libOpenGL.so.0()(64bit)")
set(
  CPACK_RPM_PACKAGE_RECOMMENDS
  "libwayland-client.so.0()(64bit), libwayland-cursor.so.0()(64bit), libwayland-egl.so.1()(64bit), libdecor-0.so.0()(64bit), libxkbcommon.so.0()(64bit), libX11.so.6()(64bit), libXext.so.6()(64bit), libXcursor.so.1()(64bit), libXfixes.so.3()(64bit), libXi.so.6()(64bit), libXrandr.so.2()(64bit), libXss.so.1()(64bit), libpulse.so.0()(64bit), libasound.so.2()(64bit), libudev.so.1()(64bit), xdg-utils"
)
# The binaries were stripped before (ViewerStrip.cmake), with the symbol
# table kept, and the symbols target holds their debug information: no
# debuginfo package, no second strip, and no /usr/lib/.build-id links,
# which two channels of one build would both claim.
set(CPACK_RPM_SPEC_INSTALL_POST "/bin/true")
set(CPACK_RPM_DEBUGINFO_PACKAGE OFF)
set(
  CPACK_RPM_SPEC_MORE_DEFINE
  "%global debug_package %{nil}
%define _build_id_links none"
)
set(
  CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /usr/share/applications
  /usr/share/metainfo
  /usr/share/icons
  /usr/share/icons/hicolor
)
foreach(
  size
  16
  32
  64
  128
  256
  512
)
  list(
    APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/icons/hicolor/${size}x${size}"
    "/usr/share/icons/hicolor/${size}x${size}/apps"
  )
endforeach()
