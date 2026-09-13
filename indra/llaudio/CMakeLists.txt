# -*- cmake -*-

add_library(llaudio STATIC)

target_sources(
  llaudio
  PRIVATE llaudioengine.cpp lllistener.cpp llaudiodecodemgr.cpp llvorbisencode.cpp llwavfile.cpp
)

target_sources(
  llaudio
  PUBLIC
    FILE_SET HEADERS
      FILES llaudioengine.h lllistener.h llaudiodecodemgr.h llvorbisencode.h llwavfile.h llwindgen.h
)

target_link_libraries(
  llaudio
  PRIVATE al::flags
  PUBLIC llcommon llmath llmessage llfilesystem ll::vorbis ll::openal ll::fmodstudio ll::faudio
)

target_precompile_headers(llaudio REUSE_FROM llprecompiled)

# The audio engines. Each ll:: target is empty when its option is off, so
# only the sources are gated.
if(AL_USE_OPENAL)
  target_sources(
    llaudio
    PRIVATE llaudioengine_openal.cpp lllistener_openal.cpp
    PUBLIC FILE_SET HEADERS FILES llaudioengine_openal.h lllistener_openal.h
  )
endif()

if(AL_USE_FMODSTUDIO)
  target_sources(
    llaudio
    PRIVATE llaudioengine_fmodstudio.cpp lllistener_fmodstudio.cpp llstreamingaudio_fmodstudio.cpp
    PUBLIC
      FILE_SET HEADERS
        FILES llaudioengine_fmodstudio.h lllistener_fmodstudio.h llstreamingaudio_fmodstudio.h
  )
endif()

if(AL_USE_FAUDIO)
  target_sources(
    llaudio
    PRIVATE llaudioengine_faudio.cpp lllistener_faudio.cpp
    PUBLIC FILE_SET HEADERS FILES llaudioengine_faudio.h lllistener_faudio.h
  )
endif()
