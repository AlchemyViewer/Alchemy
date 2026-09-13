# -*- cmake -*-
#
# What the viewer ships, and where. These rules are the package manifest:
# `cmake --install <build> --config <cfg> --prefix <dir>` writes the tree the
# viewer runs from, the viewer's POST_BUILD step stages it into the build tree
# the same way, and CPack (ViewerPackage.cmake) archives it.
#
# The tree per platform:
#   Windows  <prefix>/AlchemyX.exe, data trees beside it, llplugin/ for the
#            media plugins and the CEF and VLC payloads.
#   Linux    <prefix>/bin/alchemy-bin, bin/llplugin/, lib/ for the shared
#            libraries, data trees at the root, the wrapper and etc/ scripts.
#   macOS    <prefix>/<Channel>.app with data in Contents/Resources, shared
#            libraries in Contents/Frameworks, and the media plugins as
#            bundles in Contents/Resources carrying their own Frameworks.
#
# Included from newview/CMakeLists.txt once every target it names exists.
include_guard()

# ---------------------------------------------------------------------------
# Names. The channel decides the application name and the package name.

string(REGEX REPLACE "^Alchemy[ ]*" "" al_channel_variant "${AL_CHANNEL}")
string(TOLOWER "${al_channel_variant}" al_channel_variant_lower)
if(al_channel_variant_lower MATCHES "^release")
  set(AL_CHANNEL_TYPE release)
elseif(al_channel_variant_lower MATCHES "^beta")
  set(AL_CHANNEL_TYPE beta)
elseif(al_channel_variant_lower MATCHES "^project")
  set(AL_CHANNEL_TYPE project)
else()
  set(AL_CHANNEL_TYPE test)
endif()

# "Alchemy Viewer" for the release channel, the channel name otherwise.
if(AL_CHANNEL_TYPE STREQUAL "release")
  set(AL_APP_NAME "Alchemy Viewer")
  string(REGEX REPLACE "^[Rr]elease[ ]*" "" al_package_suffix "${al_channel_variant}")
else()
  set(AL_APP_NAME "Alchemy ${al_channel_variant}")
  set(al_package_suffix "${al_channel_variant}")
endif()
string(REPLACE " " "" AL_APP_NAME_ONEWORD "${AL_APP_NAME}")

# Alchemy[_<variant words>]_<major>_<minor>_<patch>_<revision>_<arch>
string(REPLACE " " "_" al_package_suffix "${al_package_suffix}")
if(al_package_suffix)
  string(PREPEND al_package_suffix "_")
endif()
string(REPLACE "." "_" al_package_version "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
set(AL_PACKAGE_NAME "Alchemy${al_package_suffix}_${al_package_version}_${ARCH}")

# ---------------------------------------------------------------------------
# Layout, relative to the prefix.

if(DARWIN)
  set(AL_INSTALL_BUNDLE "$<TARGET_BUNDLE_DIR_NAME:${AL_VIEWER_BINARY_NAME}>")
  set(AL_INSTALL_DATADIR "${AL_INSTALL_BUNDLE}/Contents/Resources")
  set(AL_INSTALL_BINDIR "${AL_INSTALL_BUNDLE}/Contents/MacOS")
  set(AL_INSTALL_LIBDIR "${AL_INSTALL_BUNDLE}/Contents/Frameworks")
  set(AL_INSTALL_PLUGINDIR "${AL_INSTALL_DATADIR}")
  set(al_ca_bundle_dir "${AL_INSTALL_DATADIR}")
elseif(LINUX)
  set(AL_INSTALL_DATADIR ".")
  set(AL_INSTALL_BINDIR "bin")
  set(AL_INSTALL_LIBDIR "lib")
  set(AL_INSTALL_PLUGINDIR "bin/llplugin")
  set(al_ca_bundle_dir "${AL_INSTALL_BINDIR}")
else()
  set(AL_INSTALL_DATADIR ".")
  set(AL_INSTALL_BINDIR ".")
  set(AL_INSTALL_LIBDIR ".")
  set(AL_INSTALL_PLUGINDIR "llplugin")
  set(al_ca_bundle_dir "${AL_INSTALL_BINDIR}")
endif()

set(al_vcpkg_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

# On Windows, llwebrtc and the VLC plugin have no Debug build.
if(WINDOWS)
  set(al_release_configurations CONFIGURATIONS OptDebug RelWithDebInfo Release)
else()
  set(al_release_configurations "")
endif()

# A relocatable Linux tree finds its shared libraries from the executable.
# The viewer links straight into the staging tree, where the install rule
# below finds it already in place. Built with the install RPATH it has
# nothing to patch there, so the rule neither deletes the binary as one with
# the wrong RPATH (file(RPATH_CHECK), which runs before the copy and would
# take the source with it) nor rewrites it. macOS does the same for its
# bundle.
if(LINUX)
  set_target_properties(
    ${AL_VIEWER_BINARY_NAME}
    PROPERTIES INSTALL_RPATH "$ORIGIN/../lib" BUILD_WITH_INSTALL_RPATH ON
  )
endif()

# ---------------------------------------------------------------------------
# The viewer and the libraries it loads.

install(
  TARGETS ${AL_VIEWER_BINARY_NAME}
  COMPONENT viewer
  RUNTIME DESTINATION "${AL_INSTALL_BINDIR}"
  BUNDLE DESTINATION .
)

if(WINDOWS)
  install(
    FILES $<TARGET_RUNTIME_DLLS:${AL_VIEWER_BINARY_NAME}>
    DESTINATION "${AL_INSTALL_BINDIR}"
    COMPONENT viewer
  )
endif()

# A shared llcommon sits beside the viewer and beside the plugins.
if(AL_BUILD_SHARED_LLCOMMON)
  install(
    TARGETS llcommon
    COMPONENT viewer
    RUNTIME DESTINATION "${AL_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${AL_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION lib COMPONENT devel EXCLUDE_FROM_ALL
  )
  if(WINDOWS)
    install(
      TARGETS llcommon
      COMPONENT plugins
      RUNTIME DESTINATION "${AL_INSTALL_PLUGINDIR}"
      ARCHIVE DESTINATION lib COMPONENT devel EXCLUDE_FROM_ALL
    )
  endif()
endif()

# al_install_shared_library(<library> [CONFIGURATIONS <config>...]): a
# library the viewer loads at run time, under every name it goes by. The
# name the linker was given is one symlink and the name the loader asks for
# is another beside it -- libhunspell-1.7.so and libhunspell-1.7.so.0, both
# to libhunspell-1.7.so.0.1.0 -- and install(FILES) keeps a symlink a
# symlink, so the file goes with its links. A DLL is one file and goes alone.
function(al_install_shared_library library)
  cmake_path(GET library PARENT_PATH directory)
  cmake_path(GET library FILENAME name)
  string(REGEX REPLACE "\\.(so|dylib)(\\..*)?$" "" stem "${name}")
  file(
    GLOB names
    "${directory}/${stem}.so*"
    "${directory}/${stem}.dylib"
    "${directory}/${stem}.*.dylib"
  )
  list(APPEND names "${library}")
  list(REMOVE_DUPLICATES names)
  install(FILES ${names} ${ARGN} DESTINATION "${AL_INSTALL_LIBDIR}" COMPONENT viewer)
endfunction()

# The LGPL ports link dynamically on every platform. Hunspell comes through
# pkg-config, which resolves the library the linker sees: the shared library
# itself on Linux and macOS, and an import library on Windows, where the DLL
# sits in vcpkg's bin directory. OpenAL is an imported target, which knows
# its own SONAME.
if(NOT AL_USE_NSSPELLCHECKER AND NOT AL_USE_WINSPELLCHECK)
  if(WINDOWS)
    file(GLOB al_hunspell_libraries "${al_vcpkg_dir}/bin/*hunspell*.dll")
    install(FILES ${al_hunspell_libraries} DESTINATION "${AL_INSTALL_LIBDIR}" COMPONENT viewer)
  else()
    foreach(library IN LISTS hunspell_LINK_LIBRARIES)
      al_install_shared_library("${library}")
    endforeach()
  endif()
endif()
if(AL_USE_OPENAL AND NOT WINDOWS)
  install(
    IMPORTED_RUNTIME_ARTIFACTS OpenAL::OpenAL
    LIBRARY DESTINATION "${AL_INSTALL_LIBDIR}" COMPONENT viewer
  )
endif()

# The SDKs from outside vcpkg ship the library the viewer loads at run time;
# FMOD's logging build serves the Debug configuration.
if(AL_USE_FMODSTUDIO AND FMOD_LIBRARY_RELEASE)
  al_install_shared_library(
    "${FMOD_LIBRARY_RELEASE}"
    CONFIGURATIONS
    OptDebug
    RelWithDebInfo
    Release
  )
  al_install_shared_library("${FMOD_LIBRARY_DEBUG}" CONFIGURATIONS Debug)
endif()
if(AL_USE_DISCORD)
  if(WINDOWS)
    install(
      FILES "${DISCORD_SDK_RUNTIME_DIR}/discord_partner_sdk.dll"
      DESTINATION "${AL_INSTALL_LIBDIR}"
      COMPONENT viewer
    )
  elseif(DARWIN)
    install(
      FILES "${DISCORD_SDK_RUNTIME_DIR}/libdiscord_partner_sdk.dylib"
      DESTINATION "${AL_INSTALL_LIBDIR}"
      COMPONENT viewer
    )
  else()
    file(GLOB al_discord_libraries "${DISCORD_SDK_RUNTIME_DIR}/libdiscord_partner_sdk.so*")
    install(FILES ${al_discord_libraries} DESTINATION "${AL_INSTALL_LIBDIR}" COMPONENT viewer)
  endif()
endif()

if(AL_USE_BUGSPLAT)
  if(WINDOWS)
    install(
      PROGRAMS "${al_vcpkg_dir}/tools/BsSndRpt64.exe"
      DESTINATION "${AL_INSTALL_BINDIR}"
      COMPONENT viewer
    )
    install(
      FILES "${al_vcpkg_dir}/bin/BugSplat64.dll" "${al_vcpkg_dir}/bin/BugSplatRc64.dll"
      DESTINATION "${AL_INSTALL_BINDIR}"
      COMPONENT viewer
    )
  elseif(DARWIN)
    install(
      DIRECTORY "${al_vcpkg_dir}/lib/BugSplat.framework"
      DESTINATION "${AL_INSTALL_LIBDIR}"
      USE_SOURCE_PERMISSIONS
      COMPONENT viewer
    )
  endif()
endif()

# ---------------------------------------------------------------------------
# Media plugins. Each is its own host executable; the CEF one carries the CEF
# runtime beside it, the VLC one its codec plugins.

if(LINUX)
  foreach(plugin media_plugin_cef media_plugin_libvlc media_plugin_gstreamer10 media_plugin_example)
    if(TARGET ${plugin})
      set_target_properties(${plugin} PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/../../lib")
    endif()
  endforeach()
endif()

foreach(plugin media_plugin_cef media_plugin_libvlc media_plugin_gstreamer10)
  if(TARGET ${plugin})
    set(configurations "")
    if(plugin STREQUAL "media_plugin_libvlc")
      set(configurations ${al_release_configurations})
    endif()
    install(
      TARGETS ${plugin}
      COMPONENT plugins
      ${configurations}
      RUNTIME DESTINATION "${AL_INSTALL_PLUGINDIR}"
      LIBRARY DESTINATION "${AL_INSTALL_PLUGINDIR}"
      BUNDLE DESTINATION "${AL_INSTALL_PLUGINDIR}"
    )
    if(WINDOWS)
      install(
        FILES $<TARGET_RUNTIME_DLLS:${plugin}>
        DESTINATION "${AL_INSTALL_PLUGINDIR}"
        ${configurations}
        COMPONENT plugins
      )
    endif()
  endif()
endforeach()

# The example plugin is a debugging aid; it ships outside the release channel.
if(TARGET media_plugin_example AND NOT AL_CHANNEL_TYPE STREQUAL "release")
  install(
    TARGETS media_plugin_example
    COMPONENT plugins
    RUNTIME DESTINATION "${AL_INSTALL_PLUGINDIR}"
    BUNDLE DESTINATION "${AL_INSTALL_PLUGINDIR}"
  )
endif()

if(TARGET media_plugin_cef)
  if(WINDOWS)
    # CEF's bootstrap, renamed so it loads media_plugin_cef.dll, then the
    # CEF runtime and resources beside it.
    set(cef_binary_dir "$<IF:$<CONFIG:Debug>,${CEF_BINARY_DIR_DEBUG},${CEF_BINARY_DIR_RELEASE}>")
    install(
      PROGRAMS "${cef_binary_dir}/bootstrap.exe"
      RENAME media_plugin_cef.exe
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
    install(TARGETS dullahan_host COMPONENT cef RUNTIME DESTINATION "${AL_INSTALL_PLUGINDIR}")
    install(
      FILES $<TARGET_RUNTIME_DLLS:dullahan_host>
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
    install(
      FILES
        "${cef_binary_dir}/chrome_elf.dll"
        "${cef_binary_dir}/d3dcompiler_47.dll"
        "${cef_binary_dir}/dxcompiler.dll"
        "${cef_binary_dir}/dxil.dll"
        "${cef_binary_dir}/libEGL.dll"
        "${cef_binary_dir}/libGLESv2.dll"
        "${cef_binary_dir}/v8_context_snapshot.bin"
        "${cef_binary_dir}/vk_swiftshader.dll"
        "${cef_binary_dir}/vk_swiftshader_icd.json"
        "${cef_binary_dir}/vulkan-1.dll"
        "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
        "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
        "${CEF_RESOURCE_DIR}/resources.pak"
        "${CEF_RESOURCE_DIR}/icudtl.dat"
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
    install(
      DIRECTORY "${CEF_RESOURCE_DIR}/locales"
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
  elseif(DARWIN)
    # The framework and the helper apps live in the CEF host's own bundle.
    set(cef_frameworks_dir "${AL_INSTALL_PLUGINDIR}/media_plugin_cef.app/Contents/Frameworks")
    install(
      DIRECTORY "${CEF_FRAMEWORK_DIR}"
      DESTINATION "${cef_frameworks_dir}"
      USE_SOURCE_PERMISSIONS
      COMPONENT cef
    )
    install(
      TARGETS
        dullahan_host
        dullahan_host_alerts
        dullahan_host_gpu
        dullahan_host_plugin
        dullahan_host_renderer
      COMPONENT cef
      BUNDLE DESTINATION "${cef_frameworks_dir}"
    )
  else()
    # CEF resolves its resources and locales beside libcef.so; the sandbox
    # helper, the host and the snapshot sit beside the plugin.
    install(TARGETS dullahan_host COMPONENT cef RUNTIME DESTINATION "${AL_INSTALL_PLUGINDIR}")
    install(
      PROGRAMS "${CEF_BINARY_DIR_RELEASE}/chrome-sandbox"
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
    install(
      FILES
        "${CEF_BINARY_DIR_RELEASE}/v8_context_snapshot.bin"
        "${CEF_BINARY_DIR_RELEASE}/vk_swiftshader_icd.json"
        "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
        "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
        "${CEF_RESOURCE_DIR}/resources.pak"
        "${CEF_RESOURCE_DIR}/icudtl.dat"
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
    install(
      DIRECTORY "${CEF_RESOURCE_DIR}/locales"
      DESTINATION "${AL_INSTALL_PLUGINDIR}"
      COMPONENT cef
    )
    install(
      FILES
        $<TARGET_FILE:unofficial::cef::libcef>
        "${CEF_BINARY_DIR_RELEASE}/v8_context_snapshot.bin"
        "${CEF_BINARY_DIR_RELEASE}/vk_swiftshader_icd.json"
        "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
        "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
        "${CEF_RESOURCE_DIR}/resources.pak"
        "${CEF_RESOURCE_DIR}/icudtl.dat"
      DESTINATION "${AL_INSTALL_LIBDIR}"
      COMPONENT cef
    )
    install(
      DIRECTORY "${CEF_BINARY_DIR_RELEASE}/"
      DESTINATION "${AL_INSTALL_LIBDIR}"
      COMPONENT cef
      FILES_MATCHING
      PATTERN "libEGL*"
      PATTERN "libGLESv2*"
      PATTERN "libvulkan*"
      PATTERN "libvk_swiftshader*"
    )
    install(
      DIRECTORY "${CEF_RESOURCE_DIR}/locales"
      DESTINATION "${AL_INSTALL_LIBDIR}"
      COMPONENT cef
    )
  endif()
endif()

if(TARGET media_plugin_libvlc)
  include(LibVLCPlugin)
  if(WINDOWS)
    install(
      DIRECTORY "${VLC_PLUGINS_DIR}/"
      DESTINATION "${AL_INSTALL_PLUGINDIR}/plugins"
      ${al_release_configurations}
      COMPONENT plugins
    )
  elseif(DARWIN)
    set(vlc_frameworks_dir "${AL_INSTALL_PLUGINDIR}/media_plugin_libvlc.app/Contents/Frameworks")
    file(GLOB vlc_libraries "${al_vcpkg_dir}/lib/libvlc*.dylib*")
    install(FILES ${vlc_libraries} DESTINATION "${vlc_frameworks_dir}" COMPONENT plugins)
    install(
      DIRECTORY "${VLC_PLUGINS_DIR}/"
      DESTINATION "${vlc_frameworks_dir}/plugins"
      COMPONENT plugins
      FILES_MATCHING
      PATTERN "*.dylib"
      PATTERN "plugins.dat"
    )
  endif()
endif()

# ---------------------------------------------------------------------------
# Data. The trees the viewer reads at run time, by the file types it reads.

set(al_newview_dir "${CMAKE_CURRENT_SOURCE_DIR}")

install(
  DIRECTORY "${al_newview_dir}/app_settings/"
  DESTINATION "${AL_INSTALL_DATADIR}/app_settings"
  COMPONENT viewer
  FILES_MATCHING
  PATTERN "*.ini"
  PATTERN "*.xml"
  PATTERN "alchemy_logo.png"
  PATTERN "shaders" EXCLUDE
  PATTERN "camera" EXCLUDE
  PATTERN "windlight" EXCLUDE
  PATTERN "filters" EXCLUDE
  PATTERN "colorlut" EXCLUDE
  PATTERN "looks" EXCLUDE
  PATTERN "poses" EXCLUDE
  PATTERN "dictionaries" EXCLUDE
)
foreach(
  tree
  shaders
  camera
  windlight
  filters
  colorlut
  looks
  poses
)
  install(
    DIRECTORY "${al_newview_dir}/app_settings/${tree}"
    DESTINATION "${AL_INSTALL_DATADIR}/app_settings"
    COMPONENT viewer
  )
endforeach()
install(
  FILES "${SCRIPTS_DIR}/messages/message_template.msg"
  DESTINATION "${AL_INSTALL_DATADIR}/app_settings"
  COMPONENT viewer
)
install(
  DIRECTORY "${al_vcpkg_dir}/share/alchemy-dictionaries/dictionaries"
  DESTINATION "${AL_INSTALL_DATADIR}/app_settings"
  COMPONENT viewer
)
install(
  DIRECTORY "${LSL_DEFINITIONS_DIR}/"
  DESTINATION "${AL_INSTALL_DATADIR}/app_settings/syntax_default"
  COMPONENT viewer
  FILES_MATCHING
  PATTERN "*.txt"
  PATTERN "*.xml"
  PATTERN "*.json"
  PATTERN "*.luau"
  PATTERN "*.yaml"
  PATTERN "*.yml"
)

install(
  DIRECTORY "${al_newview_dir}/character/"
  DESTINATION "${AL_INSTALL_DATADIR}/character"
  COMPONENT viewer
  FILES_MATCHING
  PATTERN "*.llm"
  PATTERN "*.xml"
  PATTERN "*.tga"
)

install(
  DIRECTORY "${ALCHEMY_FONTS_DIR}/"
  DESTINATION "${AL_INSTALL_DATADIR}/fonts"
  COMPONENT viewer
  FILES_MATCHING
  PATTERN "*.otf"
  PATTERN "*.ttc"
  PATTERN "*.ttf"
  PATTERN "*.woff2"
  PATTERN "*.txt"
)

install(
  DIRECTORY "${al_newview_dir}/skins/"
  DESTINATION "${AL_INSTALL_DATADIR}/skins"
  COMPONENT viewer
  FILES_MATCHING
  PATTERN "*.xml"
  PATTERN "*.json"
  PATTERN "*.png"
  PATTERN "*.jpg"
  PATTERN "*.tga"
  PATTERN "*.j2c"
  PATTERN "*.gif"
  PATTERN "*.html"
  PATTERN "*.js"
)

# Generated: the contributor and supporter lists and the third-party
# attribution for the About floater, the install-time settings, the build
# description, and every licence text.
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/contributors.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/supporters.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/packages-info.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/settings_install.xml"
  DESTINATION "${AL_INSTALL_DATADIR}/app_settings"
  COMPONENT viewer
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/build_data.json" "${CMAKE_CURRENT_BINARY_DIR}/licenses.txt"
  DESTINATION "${AL_INSTALL_DATADIR}"
  COMPONENT viewer
)

install(FILES "${al_newview_dir}/cube.dae" DESTINATION "${AL_INSTALL_DATADIR}" COMPONENT viewer)
install(
  FILES "${INDRA_SOURCE_DIR}/externals/ca-certificates/ca-bundle.crt"
  DESTINATION "${al_ca_bundle_dir}"
  COMPONENT viewer
)

if(WINDOWS)
  set(al_platform_suffix "")
elseif(DARWIN)
  set(al_platform_suffix "_mac")
else()
  set(al_platform_suffix "_linux")
endif()
install(
  FILES "${al_newview_dir}/featuretable${al_platform_suffix}.txt"
  DESTINATION "${AL_INSTALL_DATADIR}"
  COMPONENT viewer
)

# ---------------------------------------------------------------------------
# Per platform.

if(WINDOWS AND AL_USE_VELOPACK)
  # The installer's icon and splash ride in the tree so the packaging step
  # finds them beside the application.
  install(
    FILES
      "${BRANDING_SOURCE_DIR}/installer/icons/install_icon.ico"
      "${BRANDING_SOURCE_DIR}/installer/splash/${ICON_PATH}/install_splash.gif"
    DESTINATION .
    COMPONENT velopack
  )
endif()

if(DARWIN)
  install(
    FILES "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy.icns"
    DESTINATION "${AL_INSTALL_DATADIR}"
    COMPONENT viewer
  )
  install(
    DIRECTORY "${al_newview_dir}/cursors_mac"
    DESTINATION "${AL_INSTALL_DATADIR}"
    COMPONENT viewer
    FILES_MATCHING
    PATTERN "*.tif"
  )
  install(
    FILES
      "${al_newview_dir}/English.lproj/language.txt"
      "${CMAKE_CURRENT_BINARY_DIR}/InfoPlist.strings"
    DESTINATION "${AL_INSTALL_DATADIR}/English.lproj"
    COMPONENT viewer
  )
  foreach(
    lproj
    German
    Japanese
    Korean
    da
    es
    fr
    hu
    it
    nl
    pl
    pt
    ru
    tr
    uk
    zh-Hans
  )
    install(
      DIRECTORY "${al_newview_dir}/${lproj}.lproj"
      DESTINATION "${AL_INSTALL_DATADIR}"
      COMPONENT viewer
    )
  endforeach()

  # Ad-hoc, or with AL_SIGNING_IDENTITY, inside out; the hosted build signs
  # in its own step.
  if(NOT DEFINED ENV{GITHUB_ACTIONS})
    install(
      CODE
        "set(AL_SIGN_BUNDLE \"\${CMAKE_INSTALL_PREFIX}/${AL_INSTALL_BUNDLE}\")
set(AL_SIGN_IDENTITY \"${AL_SIGNING_IDENTITY}\")
set(AL_SIGN_PLUGIN_ENTITLEMENTS \"${al_newview_dir}/slplugin.entitlements\")
set(AL_SIGN_HELPER_ENTITLEMENTS \"${INDRA_SOURCE_DIR}/dullahan/src/dullahan.entitlements\")"
      COMPONENT viewer
    )
    install(SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ViewerCodeSign.cmake" COMPONENT viewer)
  endif()
endif()

if(LINUX)
  install(
    PROGRAMS "${al_newview_dir}/linux_tools/wrapper.sh"
    RENAME alchemy
    DESTINATION .
    COMPONENT viewer
  )
  install(PROGRAMS "${al_newview_dir}/linux_tools/install.sh" DESTINATION . COMPONENT viewer)
  install(
    PROGRAMS
      "${al_newview_dir}/linux_tools/handle_secondlifeprotocol.sh"
      "${al_newview_dir}/linux_tools/register_secondlifeprotocol.sh"
      "${al_newview_dir}/linux_tools/refresh_desktop_app_entry.sh"
      "${al_newview_dir}/linux_tools/chrome_sandboxing_permissions_setup.sh"
    DESTINATION etc
    COMPONENT viewer
  )
  install(DIRECTORY "${al_newview_dir}/res-sdl" DESTINATION . COMPONENT viewer)
  install(
    FILES "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy_256.png"
    RENAME alchemy_icon.png
    DESTINATION .
    COMPONENT viewer
  )
endif()
