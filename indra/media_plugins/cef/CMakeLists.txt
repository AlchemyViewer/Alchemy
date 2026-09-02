# -*- cmake -*-
include(PluginAPI)
include(CEFPlugin)
include(GLIB)

### media_plugin_cef

set(media_plugin_cef_SOURCE_FILES
    media_plugin_cef.cpp
    )

set(media_plugin_cef_HEADER_FILES
    volume_catcher.h
    )

# Select which VolumeCatcher implementation to use
if (FALSE) # Disable linux volume catcher due to issues dlopening with CEF 139
  foreach( PULSE_FILE pulse/introspect.h pulse/context.h pulse/subscribe.h pulse/glib-mainloop.h )
    find_path( PULSE_FILE_${PULSE_FILE}_FOUND ${PULSE_FILE} NO_CACHE)
    if( NOT PULSE_FILE_${PULSE_FILE}_FOUND )
      message( "Looking for ${PULSE_FILE} ... not found")
      message( FATAL_ERROR "Pulse header not found" )
    else()
      message( "Looking for ${PULSE_FILE} ... found")
    endif()
  endforeach()

  include(FindPipeWire)
  include_directories(SYSTEM ${PIPEWIRE_INCLUDE_DIRS} ${SPA_INCLUDE_DIRS})

  message( "Building with Linux volume catcher for PipeWire and PulseAudio" )

  list(APPEND media_plugin_cef_HEADER_FILES
    linux/volume_catcher_linux.h
  )

  set(LINUX_VOLUME_CATCHER
    linux/volume_catcher_linux.cpp
    linux/volume_catcher_pulseaudio.cpp
    linux/volume_catcher_pipewire.cpp
  )

  list(APPEND media_plugin_cef_SOURCE_FILES ${LINUX_VOLUME_CATCHER})
elseif (WINDOWS)
  list(APPEND media_plugin_cef_SOURCE_FILES windows_volume_catcher.cpp)
else()
  list(APPEND media_plugin_cef_SOURCE_FILES volume_catcher_null.cpp)
endif ()

list(APPEND media_plugin_cef_SOURCE_FILES ${media_plugin_cef_HEADER_FILES})

# Compile the plugin sources once into an object library, statically linked into
# the media_plugin_cef host executable below. Static linking avoids dlopen of
# libcef (which exhausts the static TLS block on Linux) and is the basis for the
# Windows-sandbox single-image host.
add_library(media_plugin_cef_objs OBJECT ${media_plugin_cef_SOURCE_FILES})
target_link_libraries(media_plugin_cef_objs PUBLIC
        media_plugin_base
        dullahan
        ll::cef
        ll::glib_headers
)

### media_plugin_cef - the CEF media plugin's dedicated host executable. It
### statically links the plugin object code plus the slplugin host driver and
### slplugin_cef.cpp (which registers the linked entry point and adds the shared
### daemon mode + CEF runtime lifecycle). There is no dlopen'd plugin library any
### more. On Windows it is a DLL exporting RunWinMain, loaded by CEF's bootstrap
### executable (shipped renamed to media_plugin_cef.exe) which sets up the
### sandbox; elsewhere it is a plain executable.
if (WINDOWS)
    add_library(media_plugin_cef MODULE
        ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin.cpp
        ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin_daemon.cpp
        slplugin_cef.cpp
        slplugin_cef_bootstrap.cpp
        $<TARGET_OBJECTS:media_plugin_cef_objs>
        )
else ()
    add_executable(media_plugin_cef
        MACOSX_BUNDLE
        ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin.cpp
        ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin_daemon.cpp
        slplugin_cef.cpp
        $<TARGET_OBJECTS:media_plugin_cef_objs>
        )
    if (DARWIN)
        target_sources(media_plugin_cef PRIVATE ${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin-objc.mm)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/llplugin/slplugin/slplugin-objc.mm
            PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)

        set_target_properties(media_plugin_cef PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/SLPluginCEF-Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.alchemyviewer.viewer.mediaplugincef"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.alchemyviewer.viewer.mediaplugincef"
        )
    endif ()
endif ()

target_link_libraries(media_plugin_cef
        media_plugin_base
        dullahan
        ll::cef
        ll::glib_headers
        llplugin
        llmessage
        llcommon
        ll::pluginlibraries
)

if (WINDOWS)
    # zero-copy paint producer: D3D11 keyed-mutex shared texture
    target_link_libraries(media_plugin_cef d3d11 dxgi)
endif ()

add_dependencies(media_plugin_cef dullahan_host)

if (WINDOWS)
  # media_plugin_cef.dll lives next to libcef.dll and the CEF bootstrap (deployed
  # below) so the CEF runtime resolves from its own dir.
  set_target_properties(media_plugin_cef
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${VIEWER_STAGING_DIR}/llplugin"
    LIBRARY_OUTPUT_DIRECTORY "${VIEWER_STAGING_DIR}/llplugin"
  )
  target_link_options(media_plugin_cef PRIVATE /IGNORE:4099)

  set(CEF_BIN_BINARY_DIR "$<IF:$<CONFIG:Debug>,${CEF_BINARY_DIR_DEBUG},${CEF_BINARY_DIR_RELEASE}>")

  # Ship CEF's bootstrap.exe renamed to media_plugin_cef.exe; matching base name
  # makes it load media_plugin_cef.dll and call our exported RunWinMain with the
  # sandbox info. The bootstrap.exe is part of the CEF Release payload copied below.
  add_custom_command(TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BIN_BINARY_DIR}/bootstrap.exe" "${VIEWER_STAGING_DIR}/llplugin/media_plugin_cef.exe"
  )

  # Copy the CEF runtime + resources to the packaging directory.
  set(CEF_BIN_FILES
    "${CEF_BIN_BINARY_DIR}/chrome_elf.dll"
    "${CEF_BIN_BINARY_DIR}/d3dcompiler_47.dll"
    "${CEF_BIN_BINARY_DIR}/dxcompiler.dll"
    "${CEF_BIN_BINARY_DIR}/dxil.dll"
    "${CEF_BIN_BINARY_DIR}/libEGL.dll"
    "${CEF_BIN_BINARY_DIR}/libGLESv2.dll"
    "${CEF_BIN_BINARY_DIR}/v8_context_snapshot.bin"
    "${CEF_BIN_BINARY_DIR}/vk_swiftshader.dll"
    "${CEF_BIN_BINARY_DIR}/vk_swiftshader_icd.json"
    "${CEF_BIN_BINARY_DIR}/vulkan-1.dll"
  )
  add_custom_command(
    TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CEF_BIN_FILES} "${VIEWER_STAGING_DIR}/llplugin"
    COMMAND_EXPAND_LISTS
  )

  set(CEF_RESOURCE_FILES
    "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
    "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
    "${CEF_RESOURCE_DIR}/resources.pak"
    "${CEF_RESOURCE_DIR}/icudtl.dat"
  )
  add_custom_command(
    TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CEF_RESOURCE_FILES} "${VIEWER_STAGING_DIR}/llplugin"
    COMMAND_EXPAND_LISTS
  )

  add_custom_command(
    TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CEF_RESOURCE_DIR}/locales" "${VIEWER_STAGING_DIR}/llplugin/locales"
  )

  add_custom_command(
    TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:dullahan_host>" "${VIEWER_STAGING_DIR}/llplugin"
    COMMAND_EXPAND_LISTS
  )

elseif (DARWIN)
    # macOS has extra helper bundles
    add_dependencies(media_plugin_cef dullahan_host_alerts dullahan_host_gpu dullahan_host_plugin dullahan_host_renderer)

    find_library(IOSURFACE_FRAMEWORK IOSurface)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(CORESERVICES_LIBRARY CoreServices)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)

    target_link_libraries(media_plugin_cef
    ${IOSURFACE_FRAMEWORK}
    ${COREGRAPHICS_FRAMEWORK}
    ${CORESERVICES_LIBRARY}     # for Component Manager calls
    ${AUDIOUNIT_LIBRARY}        # for AudioUnit calls
    )

elseif (LINUX)
  target_link_options(media_plugin_cef PRIVATE  "LINKER:--build-id" "LINKER:-rpath,'$ORIGIN:$ORIGIN/../../lib'")

  set(CEF_RESOURCE_FILES
    "${CEF_RESOURCE_DIR}/chrome_100_percent.pak"
    "${CEF_RESOURCE_DIR}/chrome_200_percent.pak"
    "${CEF_RESOURCE_DIR}/resources.pak"
    "${CEF_RESOURCE_DIR}/icudtl.dat"
  )
  add_custom_command(
    TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VIEWER_STAGING_DIR}/bin/llplugin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CEF_RESOURCE_FILES} "${VIEWER_STAGING_DIR}/bin/llplugin"
    COMMAND_EXPAND_LISTS
  )
  add_custom_command(
    TARGET media_plugin_cef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CEF_RESOURCE_DIR}/locales" "${VIEWER_STAGING_DIR}/bin/llplugin/locales"
  )

  if (CEF_BIN_FILES)
    add_custom_command(
      TARGET media_plugin_cef POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${VIEWER_STAGING_DIR}/lib"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CEF_BIN_FILES} "${VIEWER_STAGING_DIR}/lib"
      COMMAND_EXPAND_LISTS
    )
  endif ()
endif ()
