# -*- cmake -*-

add_library(llxml STATIC)

target_sources(llxml
    PRIVATE
    alxmldocument.cpp
    alxmllayermerge.cpp
    alxmlmergelog.cpp
    llcontrol.cpp
    llxmlnode.cpp
    llxmltree.cpp
    )

target_sources(llxml
    PUBLIC
    FILE_SET HEADERS
    FILES
    alxmldocument.h
    alxmllayermerge.h
    alxmlmergelog.h
    llcontrol.h
    llxmlnode.h
    llxmltree.h
    )

# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
target_link_libraries(llxml
    PRIVATE
    al::flags
    PUBLIC
    llfilesystem
    llmath
    llcommon
    ll::pugixml
    )
target_precompile_headers(llxml REUSE_FROM llprecompiled)

# tests
if (AL_BUILD_TESTS)
    al_add_test(llxmlnode UNIT PROJECT llxml SOURCES alxmldocument.cpp LIBRARIES ll::pugixml)
    al_add_test(llcontrol PROJECT llxml LIBRARIES llxml llmath llcommon)
    al_add_test(alxmllayermerge PROJECT llxml LIBRARIES llxml llmath llcommon)
endif ()
