# -*- cmake -*-
#
# CPack's project configuration, read once for each generator it runs with
# CPACK_GENERATOR set to that one. The .deb and the .rpm install the tree
# under /opt/<package>; the archive holds it at its top.
#
# Named by CPACK_PROJECT_CONFIG_FILE in ViewerPackageLinux.cmake.

if(CPACK_GENERATOR STREQUAL "DEB" OR CPACK_GENERATOR STREQUAL "RPM")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/${CPACK_AL_LINUX_PACKAGE}")
endif()
