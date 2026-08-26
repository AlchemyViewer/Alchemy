# -*- cmake -*-

set(llui_libtest_SOURCE_FILES
    alparamfingerprint.cpp
    llui_libtest.cpp
    llwidgetreg.cpp
    )

set(llui_libtest_HEADER_FILES
    CMakeLists.txt
    alparamfingerprint.h
    llui_libtest.h
    llwidgetreg.h
    )

list(APPEND llui_libtest_SOURCE_FILES ${llui_libtest_HEADER_FILES})

add_executable(llui_libtest ${llui_libtest_SOURCE_FILES})

# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
target_link_libraries(llui_libtest
        llui
        llinventory
        llmessage
        llrender
        llimage
        llimagej2coj
        )

# The llkdu target only exists in a proprietary build.
if (USE_KDU)
    target_link_libraries(llui_libtest llkdu)
endif (USE_KDU)

set_target_properties(llui_libtest
                    PROPERTIES
                    FOLDER "Tests"
                    )

# The app-data directory used to be reached with a relative path guessed from
# the executable's location, which stopped matching the build layout long ago.
# Bake in the real one instead.
target_compile_definitions(llui_libtest PRIVATE
        "LLUI_LIBTEST_APP_RO_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../../newview\""
        )

if (BUILD_VIEWER)
    # Ensure people working on the viewer don't break this library
    # *NOTE: This could be removed, or only built by Parabuild, if the build
    # and link times become too long. JC
    add_dependencies(viewer llui_libtest)
endif (BUILD_VIEWER)
