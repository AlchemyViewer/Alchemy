# -*- cmake -*-

# The one wrapper of the vendor library. libwebrtc ships no Debug build on
# Windows, so this target is not built for that configuration; ll::webrtc
# tells the viewer so.
add_library(llwebrtc STATIC)

target_sources(
  llwebrtc
  PRIVATE
    alaudiodevicenotifier.cpp
    alaudioechobuffer.cpp
    llwebrtc.cpp
    alaudiodevicenotifier.h
    alaudioechobuffer.h
    llwebrtc_impl.h
  PUBLIC FILE_SET HEADERS FILES llwebrtc.h
)

target_link_libraries(llwebrtc PRIVATE al::flags)

# WebRTC is built with RTTI disabled (-fno-rtti / /GR-). Subclasses such as
# LLWebRTCLogSink / LLCustomProcessor need matching flags or the linker looks
# for typeinfo symbols that libwebrtc.a does not provide.
if(WINDOWS)
  target_compile_options(llwebrtc PRIVATE /GR-)
else()
  target_compile_options(llwebrtc PRIVATE -fno-rtti)
endif()

target_link_libraries(llwebrtc PRIVATE unofficial::webrtc::webrtc)

if(WINDOWS)
  set_target_properties(llwebrtc PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD_DEBUG ON)
  target_link_libraries(
    llwebrtc
    PRIVATE secur32 winmm dmoguids wmcodecdspuuid msdmo strmiids iphlpapi ole32
  )
elseif(DARWIN)
  target_link_libraries(
    llwebrtc
    PRIVATE
      ll::oslibraries
      "-framework AVFoundation"
      "-framework AudioToolbox"
      "-framework CoreAudio"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework Foundation"
  )
  target_compile_options(llwebrtc PRIVATE -Wno-deprecated-declarations) # webrtc::CreateAudioDeviceWithDataObserver is deprecated
else()
  target_link_libraries(llwebrtc PRIVATE ${CMAKE_DL_LIBS})
  target_compile_options(llwebrtc PRIVATE -Wno-deprecated-declarations) # webrtc::CreateAudioDeviceWithDataObserver is deprecated
endif()
