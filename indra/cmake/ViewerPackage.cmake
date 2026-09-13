# -*- cmake -*-
#
# The archive of the installed tree, and the Velopack packaging step. CPack
# reads the install rules in ViewerInstall.cmake; `cpack -C <cfg>` in the
# build directory, or the `package` target, writes
# <build>/<AL_PACKAGE_NAME>.{zip,tar.xz,dmg}. The `velopack` target installs
# into <build>/newview/velopack/<cfg>/app and runs vpk over it.
#
# Included from newview/CMakeLists.txt after ViewerInstall.cmake.
include_guard()

set(al_velopack_authors "Alchemy Viewer Project")
set(al_velopack_splash_color "#00a5dc")
set(al_velopack_version "${VIEWER_SHORT_VERSION}-${VIEWER_VERSION_REVISION}")
if(DARWIN)
  set(al_velopack_title "${AL_APP_NAME}")
  set(al_velopack_main_exe "${product}")
else()
  set(al_velopack_title "${AL_APP_NAME_ONEWORD}")
  set(al_velopack_main_exe "${OUTPUT_BINARY_NAME}.exe")
endif()

# What the hosted build's packaging and signing steps need to know, one
# key=value per line for GITHUB_OUTPUT.
if(DARWIN)
  set(
    al_package_env
    "velopack_mac_pack_id=${AL_APP_NAME_ONEWORD}
velopack_mac_pack_version=${al_velopack_version}
velopack_mac_pack_title=${al_velopack_title}
velopack_mac_main_exe=${al_velopack_main_exe}
velopack_mac_bundle_id=${MACOSX_BUNDLE_GUI_IDENTIFIER}
"
  )
elseif(WINDOWS)
  set(
    al_package_env
    "velopack_pack_id=${AL_APP_NAME_ONEWORD}
velopack_pack_version=${al_velopack_version}
velopack_pack_title=${al_velopack_title}
velopack_pack_authors=${al_velopack_authors}
velopack_main_exe=${al_velopack_main_exe}
velopack_icon=install_icon.ico
velopack_splash=install_splash.gif
velopack_splash_color=${al_velopack_splash_color}
velopack_installer_base=${AL_PACKAGE_NAME}
"
  )
else()
  set(al_package_env "")
endif()
file(
  CONFIGURE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/package.env"
  CONTENT
    "imagename=${AL_PACKAGE_NAME}
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
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_SOURCE_GENERATOR "")
set(CPACK_ARCHIVE_THREADS 0)

if(WINDOWS)
  set(CPACK_GENERATOR ZIP)
elseif(DARWIN)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME "${AL_APP_NAME}")
  set(
    CPACK_DMG_BACKGROUND_IMAGE
    "${CMAKE_CURRENT_SOURCE_DIR}/installers/darwin/release-dmg/background.jpg"
  )
  set(CPACK_DMG_DS_STORE "${CMAKE_CURRENT_SOURCE_DIR}/installers/darwin/release-dmg/_DS_Store")
else()
  set(CPACK_GENERATOR TXZ)
endif()

# Release archives on Linux and macOS are stripped of debug information
# after the install into the package staging area.
if(NOT WINDOWS)
  set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/ViewerStrip.cmake")
endif()

include(CPack)

if(AL_USE_VELOPACK)
  set(velopack_dir "${CMAKE_CURRENT_BINARY_DIR}/velopack/$<CONFIG>")
  set(velopack_releases "${velopack_dir}/Releases")
  if(DARWIN)
    set(
      velopack_args
      --packDir
      "${velopack_dir}/app/${AL_INSTALL_BUNDLE}"
      --mainExe
      "${al_velopack_main_exe}"
      --bundleId
      "${MACOSX_BUNDLE_GUI_IDENTIFIER}"
      --icon
      "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy.icns"
      --noInst
    )
    set(velopack_rename "")
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
    )
    set(
      velopack_rename
      COMMAND
      ${CMAKE_COMMAND}
      -E
      rename
      "${velopack_releases}/${AL_APP_NAME_ONEWORD}-win-Setup.exe"
      "${velopack_releases}/${AL_PACKAGE_NAME}_Setup.exe"
      COMMAND
      ${CMAKE_COMMAND}
      -E
      rename
      "${velopack_releases}/${AL_APP_NAME_ONEWORD}-win-Portable.zip"
      "${velopack_releases}/${AL_PACKAGE_NAME}_Portable.zip"
    )
  endif()
  add_custom_target(
    velopack
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${velopack_dir}"
    COMMAND
      ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config $<CONFIG> --prefix
      "${velopack_dir}/app"
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
