# -*- cmake -*-

include(Variables)
include(FreeType)

set(LLRENDER_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llrender
    )

if (BUILD_HEADLESS)
  set(LLRENDER_HEADLESS_LIBRARIES
      llrenderheadless
      )
endif (BUILD_HEADLESS)
set(LLRENDER_LIBRARIES
    llrender
    )

