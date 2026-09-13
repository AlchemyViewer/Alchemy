# -*- cmake -*-

add_library(llplugin STATIC)

target_sources(
  llplugin
  PRIVATE
    llpluginclassmedia.cpp
    llplugininstance.cpp
    llpluginmessage.cpp
    llpluginmessagepipe.cpp
    llpluginprocesschild.cpp
    llpluginprocessparent.cpp
    llpluginsharedmemory.cpp
  PUBLIC
    FILE_SET HEADERS
      FILES
        llpluginclassmedia.h
        llpluginclassmediaowner.h
        llplugininstance.h
        llpluginmessage.h
        llpluginmessageclasses.h
        llpluginmessagepipe.h
        llpluginprocesschild.h
        llpluginprocessparent.h
        llpluginsharedmemory.h
)

target_link_libraries(llplugin PRIVATE al::flags PUBLIC llcommon llmath llmessage llxml)
target_precompile_headers(llplugin REUSE_FROM llprecompiled)

add_subdirectory(slplugin)
