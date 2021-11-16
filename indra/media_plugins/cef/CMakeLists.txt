# -*- cmake -*-

project(media_plugin_cef)

include(Boost)
include(00-Common)
include(LLCommon)
include(LLPlugin)
include(LLMath)
include(LLRender)
include(LLWindow)
include(Linking)
include(PluginAPI)
include(MediaPluginBase)

include(CEFPlugin)

include_directories(
    ${LLPLUGIN_INCLUDE_DIRS}
    ${MEDIA_PLUGIN_BASE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLRENDER_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${CEF_INCLUDE_DIR}
)
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    )


### media_plugin_cef

set(media_plugin_cef_SOURCE_FILES
    media_plugin_cef.cpp
    )

set(media_plugin_cef_HEADER_FILES
    volume_catcher.h
    )

set (media_plugin_cef_LINK_LIBRARIES
  ${LLPLUGIN_LIBRARIES}
  ${MEDIA_PLUGIN_BASE_LIBRARIES}
  ${CEF_PLUGIN_LIBRARIES}
  ${LLCOMMON_LIBRARIES}
  ${PLUGIN_API_WINDOWS_LIBRARIES})

# Select which VolumeCatcher implementation to use
if (LINUX)
  if (PULSEAUDIO_FOUND)
    list(APPEND media_plugin_cef_SOURCE_FILES linux_volume_catcher.cpp)
  else (PULSEAUDIO_FOUND)
    list(APPEND media_plugin_cef_SOURCE_FILES dummy_volume_catcher.cpp)
  endif (PULSEAUDIO_FOUND)
  list(APPEND media_plugin_webkit_LINK_LIBRARIES
       ${UI_LIBRARIES}     # for glib/GTK
       )
elseif (DARWIN)
  list(APPEND media_plugin_cef_SOURCE_FILES mac_volume_catcher_null.cpp)
  find_library(CORESERVICES_LIBRARY CoreServices)
  find_library(AUDIOUNIT_LIBRARY AudioUnit)
  list(APPEND media_plugin_cef_LINK_LIBRARIES
       ${CORESERVICES_LIBRARY}     # for Component Manager calls
       ${AUDIOUNIT_LIBRARY}        # for AudioUnit calls
  )
elseif (WINDOWS)
  list(APPEND media_plugin_cef_SOURCE_FILES windows_volume_catcher.cpp)
endif (LINUX)

set_source_files_properties(${media_plugin_cef_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND media_plugin_cef_SOURCE_FILES ${media_plugin_cef_HEADER_FILES})

add_library(media_plugin_cef
    SHARED
    ${media_plugin_cef_SOURCE_FILES}
    )

set_target_properties(media_plugin_cef PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_link_libraries(media_plugin_cef
  ${media_plugin_cef_LINK_LIBRARIES}
)

if (WINDOWS)
  set_target_properties(
    media_plugin_cef
    PROPERTIES
    LINK_FLAGS "/MANIFEST:NO /NODEFAULTLIB:LIBCMT /IGNORE:4099"
    LINK_FLAGS_DEBUG "/MANIFEST:NO /NODEFAULTLIB:LIBCMTD /IGNORE:4099"
    )
endif (WINDOWS)

if (DARWIN)
  # Don't prepend 'lib' to the executable name, and don't embed a full path in the library's install name
  set_target_properties(
    media_plugin_cef
    PROPERTIES
    PREFIX ""
    LINK_FLAGS "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/../base/media_plugin_base.exp"
  )

  ## turns on C++11 using Cmake
  target_compile_features(media_plugin_cef PRIVATE cxx_range_for)
endif (DARWIN)
