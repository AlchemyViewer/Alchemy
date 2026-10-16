# -*- cmake -*-
#
# What the viewer links for WebRTC voice: the llwebrtc library where one is
# built, and the DISABLE_WEBRTC fact where it is not. libwebrtc ships no
# Debug build on Windows, so that configuration compiles without voice and
# links nothing. llwebrtc itself, the one wrapper of the vendor library,
# links unofficial::webrtc::webrtc directly.
include_guard()
add_library(ll::webrtc INTERFACE IMPORTED)

if(NOT AL_USE_WEBRTC)
  target_compile_definitions(ll::webrtc INTERFACE DISABLE_WEBRTC=1)
else()
  find_package(unofficial-webrtc CONFIG REQUIRED)
  if(WINDOWS)
    target_compile_definitions(ll::webrtc INTERFACE $<$<CONFIG:Debug>:DISABLE_WEBRTC=1>)
    # A target name inside a configuration expression on an imported
    # interface reaches the link line but not the build order: the viewer's
    # project carried no reference to llwebrtc, and a build of the viewer
    # alone failed to find its library. Dependencies added to an interface
    # target are followed in its place by whatever links it.
    target_link_libraries(ll::webrtc INTERFACE $<$<NOT:$<CONFIG:Debug>>:llwebrtc>)
    add_dependencies(ll::webrtc llwebrtc)
  else()
    target_link_libraries(ll::webrtc INTERFACE llwebrtc)
  endif()
endif()
