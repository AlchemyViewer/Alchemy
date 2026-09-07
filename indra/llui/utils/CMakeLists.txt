# -*- cmake -*-

# The console utilities over the UI. What each is for, and how it is run,
# is at the top of its own file.

set(llui_libtest_SOURCE_FILES
    alparamfingerprint.cpp
    llui_libtest.cpp
    llwidgetreg.cpp
    alparamfingerprint.h
    llui_libtest.h
    llwidgetreg.h
    )

add_executable(llui_libtest ${llui_libtest_SOURCE_FILES})

# Libraries on which this depends, needed for Linux builds.
# Sort by high-level to low-level.
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
    RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
    FOLDER "Utils"
    )

# The app-data directory used to be reached with a relative path guessed from
# the executable's location, which stopped matching the build layout long ago.
# Bake in the real one instead.
target_compile_definitions(llui_libtest PRIVATE
        "LLUI_LIBTEST_APP_RO_DIR=\"${INDRA_SOURCE_DIR}/newview\""
        )

# The viewer used to depend on this one, from a directory configured after
# newview; beside the library it is about, that target does not exist yet.
# It builds with the tests instead, which is where it is run from.

# The overlay census: the viewer's own merge over every skin and language
# of the source tree, printed by language and gated by the committed
# baseline. It writes nothing but its own report, so it is the ctest that
# guards the merge as well as the instrument a repair pass is measured
# with.
add_executable(xui_overlay_check xui_overlay_check.cpp)
target_link_libraries(xui_overlay_check llxml llmessage llcorehttp llcommon)
set_target_properties(xui_overlay_check
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
    FOLDER "Utils"
    )

# The translation repair over a whole language, which is XUI Studio's
# own action from a console: a repair pass is then a command and the diff
# it leaves.
add_executable(xui_translate_repair xui_translate_repair.cpp)
target_link_libraries(xui_translate_repair llui llmessage llcorehttp llxml llrender llcommon)
set_target_properties(xui_translate_repair
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
    FOLDER "Utils"
    )

if (BUILD_TESTING)
    add_dependencies(BUILD_TESTS llui_libtest xui_overlay_check xui_translate_repair)
    add_test(NAME xui_overlay_check
        COMMAND xui_overlay_check "${INDRA_SOURCE_DIR}/newview/skins"
                "${CMAKE_CURRENT_SOURCE_DIR}/xui_overlay_baseline.txt" --examples 0)
endif ()
