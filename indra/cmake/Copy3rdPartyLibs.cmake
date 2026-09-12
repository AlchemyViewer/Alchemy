# -*- cmake -*-
#
# Stages the third-party shared libraries vcpkg does not place next to the
# viewer itself: the FMOD Studio and Discord SDK libraries, which come from
# outside vcpkg, and the hunspell dylib on macOS. Everything else a static
# vcpkg build needs is linked in.
include_guard()
include(CMakeCopyIfDifferent)
include(Linking)

# Pass FROM_DIR, TARGETS and the files to copy; TO_DIR is the shared-library
# staging directory. Appends the generated outputs to TARGETS.
macro(to_staging_dirs from_dir targets)
    copy_if_different("${from_dir}" "${SHARED_LIB_STAGING_DIR}" out_targets ${ARGN})
    list(APPEND "${targets}" "${out_targets}")
endmacro()

macro(to_viewer_staging_dirs from_dir targets)
    copy_if_different("${from_dir}" "${VIEWER_STAGING_DIR}" out_targets ${ARGN})
    list(APPEND "${targets}" "${out_targets}")
endmacro()

if(WINDOWS AND AL_USE_FMODSTUDIO)
    to_viewer_staging_dirs(
        ${fmod_lib_paths}
        third_party_targets
        fmod$<$<CONFIG:Debug>:L>.dll
        )
endif()

if(WINDOWS AND AL_USE_DISCORD)
    to_viewer_staging_dirs(
        "${DISCORD_SDK_RUNTIME_DIR}"
        third_party_targets
        discord_partner_sdk.dll
        )
endif()

if(DARWIN AND NOT AL_USE_NSSPELLCHECKER)
    to_staging_dirs(
        "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
        third_party_targets
        libhunspell-1.7.0.dylib
        )
endif()

add_custom_target(
        stage_third_party_libs ALL
        DEPENDS ${third_party_targets}
)
