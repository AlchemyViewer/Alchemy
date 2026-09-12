# -*- cmake -*-

# llappearance and, when AL_BUILD_HEADLESS is on, llappearanceheadless are the
# same sources against different render libraries.
set(llappearance_SOURCES
    llavatarappearance.cpp
    llavatarjoint.cpp
    llavatarjointmesh.cpp
    lldriverparam.cpp
    lllocaltextureobject.cpp
    llpolyskeletaldistortion.cpp
    llpolymesh.cpp
    llpolymorph.cpp
    lltexglobalcolor.cpp
    lltexlayer.cpp
    lltexlayerparams.cpp
    llwearable.cpp
    llwearabledata.cpp
    llwearabletype.cpp
    llviewervisualparam.cpp
    llavatarappearancedefines.cpp
    )

set(llappearance_HEADERS
    llavatarappearance.h
    llavatarjoint.h
    llavatarjointmesh.h
    lldriverparam.h
    lljointdata.h
    lljointpickname.h
    lllocaltextureobject.h
    llpolyskeletaldistortion.h
    llpolymesh.h
    llpolymorph.h
    lltexglobalcolor.h
    lltexlayer.h
    lltexlayerparams.h
    llwearable.h
    llwearabledata.h
    llwearabletype.h
    llviewervisualparam.h
    llavatarappearancedefines.h
    )

add_library(llappearance STATIC)

target_sources(llappearance
    PRIVATE
    ${llappearance_SOURCES}
    PUBLIC
    FILE_SET HEADERS
    FILES
    ${llappearance_HEADERS}
    )

target_link_libraries(llappearance
        PRIVATE
        al::flags
        PUBLIC
        llcharacter
        llinventory
        llimage
        llrender
        llfilesystem
        llmath
        llxml
        llmessage
        llcorehttp
        llcommon
    )
target_precompile_headers(llappearance REUSE_FROM llprecompiled)

if (AL_BUILD_HEADLESS)
  add_library(llappearanceheadless STATIC)

  target_sources(llappearanceheadless
      PRIVATE
      ${llappearance_SOURCES}
      PUBLIC
      FILE_SET HEADERS
      FILES
      ${llappearance_HEADERS}
      )

  target_link_libraries(llappearanceheadless
          PRIVATE
          al::flags
          PUBLIC
          llcharacter
          llinventory
          llimage
          llrenderheadless
          llfilesystem
          llmath
          llxml
          llmessage
          llcorehttp
          llcommon
      )

  target_precompile_headers(llappearanceheadless REUSE_FROM llprecompiled)
endif ()
