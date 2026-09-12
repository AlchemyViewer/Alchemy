# -*- cmake -*-

# The one wrapper of the vendor library. libwebrtc ships no Debug build on
# Windows, so this target is not built for that configuration; ll::webrtc
# tells the viewer so.
add_library(llwebrtc SHARED)

target_sources(llwebrtc
    PRIVATE
    alaudiodevicenotifier.cpp
    alaudioechobuffer.cpp
    llwebrtc.cpp
    alaudiodevicenotifier.h
    alaudioechobuffer.h
    llwebrtc_impl.h
    PUBLIC
    FILE_SET HEADERS
    FILES
    llwebrtc.h
    )

set_target_properties(llwebrtc PROPERTIES PUBLIC_HEADER llwebrtc.h)
target_link_libraries(llwebrtc PRIVATE al::flags)

# WebRTC is built with RTTI disabled (-fno-rtti / /GR-). Subclasses such as
# LLWebRTCLogSink / LLCustomProcessor need matching flags or the linker looks
# for typeinfo symbols that libwebrtc.a does not provide.
if (WINDOWS)
    target_compile_options(llwebrtc PRIVATE /GR-)
else()
    target_compile_options(llwebrtc PRIVATE -fno-rtti)
endif()

target_link_libraries(llwebrtc PRIVATE unofficial::webrtc::webrtc)

if (WINDOWS)
    set_target_properties(llwebrtc
        PROPERTIES
        EXCLUDE_FROM_DEFAULT_BUILD_DEBUG ON
        RUNTIME_OUTPUT_DIRECTORY "${VIEWER_STAGING_DIR}"
        )

    target_link_options(llwebrtc PRIVATE /MANIFEST:NO)
    target_link_libraries(llwebrtc PRIVATE
                                       secur32
                                       winmm
                                       dmoguids
                                       wmcodecdspuuid
                                       msdmo
                                       strmiids
                                       iphlpapi
                                       ole32)

    if (AL_USE_BUGSPLAT)
        set_target_properties(llwebrtc PROPERTIES PDB_OUTPUT_DIRECTORY "${SYMBOLS_STAGING_DIR}")
    endif (AL_USE_BUGSPLAT)
elseif(DARWIN)
    target_link_libraries(llwebrtc PRIVATE ll::oslibraries
                                   "-framework AVFoundation"
                                   "-framework AudioToolbox"
                                   "-framework CoreAudio"
                                   "-framework CoreMedia"
                                   "-framework CoreVideo"
                                   "-framework Foundation"
    )
    target_compile_options(llwebrtc PRIVATE -Wno-deprecated-declarations) # webrtc::CreateAudioDeviceWithDataObserver is deprecated


    if (AL_USE_BUGSPLAT)
        set_target_properties(llwebrtc PROPERTIES XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
                                                  XCODE_ATTRIBUTE_DWARF_DSYM_FOLDER_PATH "${SYMBOLS_STAGING_DIR}/dSYMs")
        if(NOT XCODE)
            find_program(DSYMUTIL dsymutil REQUIRED)
            add_custom_command(TARGET llwebrtc POST_BUILD
                    COMMAND dsymutil $<TARGET_FILE:llwebrtc>
                    COMMENT "Generating llwebrtc.dSYM")
        endif()
    endif (AL_USE_BUGSPLAT)
else()
    target_link_libraries(llwebrtc PRIVATE ${CMAKE_DL_LIBS})
    target_compile_options(llwebrtc PRIVATE -Wno-deprecated-declarations) # webrtc::CreateAudioDeviceWithDataObserver is deprecated
endif (WINDOWS)
