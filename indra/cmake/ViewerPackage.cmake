# -*- cmake -*-
#
# The archive of the installed tree, the source package, and the Velopack
# packaging step. CPack reads the install rules in ViewerInstall.cmake;
# `cpack -C <cfg>` in the build directory, or the `package` target, writes
# <build>/<AL_PACKAGE_NAME>.{zip,tar.xz,dmg}, and on Linux the .deb and .rpm
# beside the archive (ViewerPackageLinux.cmake); `cpack --config
# CPackSourceConfig.cmake`, or the `package_source` target where the
# generator has one, writes <build>/Alchemy_<version>_src.tar.xz from what
# git tracks. Each comes with its SHA-256 beside it. The `velopack` target
# installs into <build>/newview/velopack/<cfg>/app, or on Linux into the
# usr/bin of an AppDir there, and runs vpk over it.
#
# Included from newview/CMakeLists.txt after ViewerInstall.cmake.
include_guard()

set(al_velopack_authors "Alchemy Viewer Project")
set(al_velopack_splash_color "#00a5dc")
set(al_velopack_version "${VIEWER_SHORT_VERSION}-${VIEWER_VERSION_REVISION}")
# An installed viewer updates from releases.<channel>.json, so each platform
# and architecture has a channel of its own, named for its runtime. The
# channel also names the files vpk writes.
set(al_velopack_os "")
if(DARWIN)
  set(al_velopack_title "${AL_APP_NAME}")
  set(al_velopack_main_exe "${product}")
  set(al_velopack_os osx)
elseif(WINDOWS)
  set(al_velopack_title "${AL_APP_NAME_ONEWORD}")
  set(al_velopack_main_exe "${OUTPUT_BINARY_NAME}.exe")
  set(al_velopack_os win)
elseif(LINUX)
  # The AppImage runs the tree's launcher; ViewerAppDir.cmake lays out the
  # AppDir around the installed tree.
  set(al_velopack_title "${AL_APP_NAME}")
  set(al_velopack_main_exe alchemy)
  set(al_velopack_os linux)
endif()

# What the hosted build's packaging jobs need to know, one key=value per
# line. The jobs export every key to the environment in upper case.
if(al_velopack_os)
  if(BUILD_TARGET_IS_ARM64)
    set(al_velopack_runtime ${al_velopack_os}-arm64)
  else()
    set(al_velopack_runtime ${al_velopack_os}-x64)
  endif()
  set(al_velopack_channel ${al_velopack_runtime})
  set(
    al_package_env
    "velopack_pack_id=${AL_APP_NAME_ONEWORD}
velopack_pack_version=${al_velopack_version}
velopack_pack_title=${al_velopack_title}
velopack_pack_authors=${al_velopack_authors}
velopack_main_exe=${al_velopack_main_exe}
velopack_channel=${al_velopack_channel}
velopack_runtime=${al_velopack_runtime}
"
  )
  if(LINUX)
    string(
      APPEND al_package_env
      "velopack_icon=share/icons/hicolor/256x256/apps/${AL_APP_ID}.png
"
    )
  endif()
  if(WINDOWS)
    string(
      APPEND al_package_env
      "velopack_icon=install_icon.ico
velopack_splash=install_splash.gif
velopack_splash_color=${al_velopack_splash_color}
"
    )
  endif()
else()
  set(al_package_env "")
endif()
set(AL_SOURCE_PACKAGE_NAME "Alchemy_${al_package_version}_src")
file(
  CONFIGURE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/package.env"
  CONTENT
    "imagename=${AL_PACKAGE_NAME}
sourcename=${AL_SOURCE_PACKAGE_NAME}
${al_package_env}"
  @ONLY
)

if(NOT AL_BUILD_PACKAGE)
  return()
endif()

set(CPACK_PACKAGE_NAME "${AL_APP_NAME}")
set(CPACK_PACKAGE_FILE_NAME "${AL_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "${al_velopack_authors}")
set(CPACK_PACKAGE_VERSION "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${VIEWER_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VIEWER_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VIEWER_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Alchemy Viewer, a client for Second Life")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://www.alchemyviewer.org")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_ARCHIVE_THREADS 0)

# The source package: the committed tree of the repository and its
# submodules. ViewerSource.cmake fills the staging directory from git
# archive, so CPack has no directory to copy and nothing to ignore.
find_package(Git QUIET)
get_filename_component(al_repository_dir "${INDRA_SOURCE_DIR}/.." ABSOLUTE)
set(CPACK_SOURCE_GENERATOR TXZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${AL_SOURCE_PACKAGE_NAME}")
set(CPACK_SOURCE_INSTALLED_DIRECTORIES "")
set(CPACK_SOURCE_IGNORE_FILES "")
set(CPACK_INSTALL_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/ViewerSource.cmake")
set(CPACK_AL_GIT "${GIT_EXECUTABLE}")
set(CPACK_AL_REPOSITORY "${al_repository_dir}")

if(WINDOWS)
  set(CPACK_GENERATOR ZIP)
elseif(DARWIN)
  # A plain disk image with the Applications link beside the bundle. The
  # hosted build makes its own, laid out, from the notarized bundle. APFS,
  # not HFS+: HFS+ decomposes file names, and the bundle's seal holds the
  # names of the font stand-ins with Japanese names as they were composed.
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME "${AL_APP_NAME}")
  set(CPACK_DMG_FILESYSTEM APFS)
  set(CPACK_DMG_FORMAT ULMO)
else()
  # The archive, and the system packages where the tools that make them are:
  # dpkg-shlibdeps, which finds a .deb's dependencies, and rpmbuild.
  set(CPACK_GENERATOR TXZ)
  find_program(AL_DPKG_SHLIBDEPS dpkg-shlibdeps)
  find_program(AL_RPMBUILD rpmbuild)
  mark_as_advanced(AL_DPKG_SHLIBDEPS AL_RPMBUILD)
  if(AL_DPKG_SHLIBDEPS)
    list(APPEND CPACK_GENERATOR DEB)
  endif()
  if(AL_RPMBUILD)
    list(APPEND CPACK_GENERATOR RPM)
  endif()
endif()

# Release archives on Linux and macOS are stripped of debug information
# after the install into the package staging area; on macOS the bundle is
# then sealed again as the install signed it.
if(NOT WINDOWS)
  set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/ViewerStrip.cmake")
endif()
if(LINUX)
  include(ViewerPackageLinux)
endif()
if(DARWIN)
  set(CPACK_AL_SIGN_IDENTITY "${AL_SIGNING_IDENTITY}")
  set(CPACK_AL_SIGN_PLUGIN_ENTITLEMENTS "${AL_SIGN_PLUGIN_ENTITLEMENTS}")
  set(CPACK_AL_SIGN_HELPER_ENTITLEMENTS "${AL_SIGN_HELPER_ENTITLEMENTS}")
endif()

include(CPack)

if(AL_USE_VELOPACK)
  set(velopack_dir "${CMAKE_CURRENT_BINARY_DIR}/velopack/$<CONFIG>")
  set(velopack_releases "${velopack_dir}/Releases")
  set(velopack_app "${velopack_dir}/app")
  set(velopack_prepare "")
  if(DARWIN)
    # The install signed every nested piece; vpk signs the updater it adds,
    # seals the bundle again with the viewer's entitlements, and notarizes it
    # when given a notarytool profile. Ad-hoc without an identity.
    if(AL_SIGNING_IDENTITY)
      set(velopack_identity "${AL_SIGNING_IDENTITY}")
    else()
      set(velopack_identity "-")
    endif()
    set(
      velopack_args
      --packDir
      "${velopack_dir}/app/${AL_INSTALL_BUNDLE}"
      --mainExe
      "${al_velopack_main_exe}"
      --channel
      "${al_velopack_channel}"
      --runtime
      "${al_velopack_runtime}"
      --noInst
      --signAppIdentity
      "${velopack_identity}"
      --signDisableDeep
      --signEntitlements
      "${AL_SIGN_PLUGIN_ENTITLEMENTS}"
    )
    if(AL_SIGNING_IDENTITY AND AL_NOTARY_PROFILE)
      list(APPEND velopack_args --notaryProfile "${AL_NOTARY_PROFILE}")
    endif()
  elseif(LINUX)
    # The tree installs into an AppDir, is stripped as the package's is, and
    # is laid out for the AppImage; vpk adds its updater and manifest.
    set(velopack_appdir "${velopack_dir}/${AL_APP_NAME_ONEWORD}.AppDir")
    set(velopack_app "${velopack_appdir}/usr/bin")
    set(
      velopack_prepare
      COMMAND
      ${CMAKE_COMMAND}
      -DCPACK_BUILD_CONFIG=$<CONFIG>
      "-DCPACK_TEMPORARY_INSTALL_DIRECTORY=${velopack_app}"
      -DCPACK_PACKAGE_FILE_NAME=AppDir
      -P
      "${CMAKE_CURRENT_LIST_DIR}/ViewerStrip.cmake"
      COMMAND
      ${CMAKE_COMMAND}
      "-DAL_APPDIR=${velopack_appdir}"
      -P
      "${CMAKE_CURRENT_LIST_DIR}/ViewerAppDir.cmake"
    )
    set(
      velopack_args
      --packDir
      "${velopack_appdir}"
      --mainExe
      "${al_velopack_main_exe}"
      --icon
      "${velopack_appdir}/${AL_APP_ID}.png"
      --channel
      "${al_velopack_channel}"
      --runtime
      "${al_velopack_runtime}"
    )
  else()
    set(
      velopack_args
      --packDir
      "${velopack_dir}/app"
      --mainExe
      "${al_velopack_main_exe}"
      --icon
      "${BRANDING_SOURCE_DIR}/installer/icons/install_icon.ico"
      --splashImage
      "${BRANDING_SOURCE_DIR}/installer/splash/${ICON_PATH}/install_splash.gif"
      --splashProgressColor
      "${al_velopack_splash_color}"
      --shortcuts
      ""
      --channel
      "${al_velopack_channel}"
      --runtime
      "${al_velopack_runtime}"
    )
  endif()
  # The Windows installer and portable archive and the Linux AppImage take
  # the package's name; vpk's names for the macOS outputs carry the channel,
  # which keeps them apart from the Windows arm64 ones on a release page.
  set(velopack_rename "")
  if(LINUX)
    set(
      velopack_rename
      COMMAND
      ${CMAKE_COMMAND}
      -E
      rename
      "${velopack_releases}/${AL_APP_NAME_ONEWORD}-${al_velopack_channel}.AppImage"
      "${velopack_releases}/${AL_PACKAGE_NAME}.AppImage"
    )
  elseif(WINDOWS)
    set(
      velopack_rename
      COMMAND
      ${CMAKE_COMMAND}
      -E
      rename
      "${velopack_releases}/${AL_APP_NAME_ONEWORD}-${al_velopack_channel}-Setup.exe"
      "${velopack_releases}/${AL_PACKAGE_NAME}_Setup.exe"
      COMMAND
      ${CMAKE_COMMAND}
      -E
      rename
      "${velopack_releases}/${AL_APP_NAME_ONEWORD}-${al_velopack_channel}-Portable.zip"
      "${velopack_releases}/${AL_PACKAGE_NAME}_Portable.zip"
    )
  endif()
  add_custom_target(
    velopack
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${velopack_dir}"
    COMMAND
      ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config $<CONFIG> --prefix "${velopack_app}"
      ${velopack_prepare}
    COMMAND
      dotnet vpk pack --packId "${AL_APP_NAME_ONEWORD}" --packVersion "${al_velopack_version}"
      --packAuthors "${al_velopack_authors}" --packTitle "${al_velopack_title}" --outputDir
      "${velopack_releases}" ${velopack_args} ${velopack_rename}
    WORKING_DIRECTORY "${INDRA_SOURCE_DIR}"
    COMMENT "Packaging with Velopack into ${velopack_releases}"
    VERBATIM
    USES_TERMINAL
  )
  add_dependencies(velopack ${AL_VIEWER_BINARY_NAME})
endif()
