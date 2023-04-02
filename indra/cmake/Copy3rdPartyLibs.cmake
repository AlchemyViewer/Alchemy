# -*- cmake -*-

# The copy_win_libs folder contains file lists and a script used to
# copy dlls, exes and such needed to run the SecondLife from within
# VisualStudio.

include(CMakeCopyIfDifferent)
include(Linking)
include(FMODSTUDIO)
include(OPENAL)
include(DiscordSDK)
include(Sentry)

# When we copy our dependent libraries, we almost always want to copy them to
# both the Release and the RelWithDebInfo staging directories. This has
# resulted in duplicate (or worse, erroneous attempted duplicate)
# copy_if_different commands. Encapsulate that usage.
# Pass FROM_DIR, TARGETS and the files to copy. TO_DIR is implicit.
# to_staging_dirs diverges from copy_if_different in that it appends to TARGETS.
macro(to_staging_dirs from_dir targets)
    set( targetDir "${SHARED_LIB_STAGING_DIR}")
    copy_if_different("${from_dir}" "${targetDir}" out_targets ${ARGN})

    list(APPEND "${targets}" "${out_targets}")
endmacro()

###################################################################
# set up platform specific lists of files that need to be copied
###################################################################
if(WINDOWS)
    #*******************************
    # VIVOX - *NOTE: no debug version
    set(vivox_lib_dir "${ARCH_PREBUILT_DIRS_RELEASE}")

    # ND, it seems there is no such thing defined. At least when building a viewer
    # Does this maybe matter on some LL buildserver? Otherwise this and the snippet using slvoice_src_dir
    # can all go
    if( ARCH_PREBUILT_BIN_DIRS_RELEASE )
        set(slvoice_src_dir "${ARCH_PREBUILT_BIN_DIRS_RELEASE}")    
    endif()
    set(slvoice_files SLVoice.exe )
    if (ADDRESS_SIZE EQUAL 64)
        list(APPEND vivox_libs
            vivoxsdk_x64.dll
            ortp_x64.dll
            )
    else (ADDRESS_SIZE EQUAL 64)
        list(APPEND vivox_libs
            vivoxsdk.dll
            ortp.dll
            )
    endif (ADDRESS_SIZE EQUAL 64)

    #*******************************
    # Misc shared libs 

    set(addrsfx "-x${ADDRESS_SIZE}")

    set(debug_src_dir "${ARCH_PREBUILT_DIRS_DEBUG}")
    set(debug_files
        openjp2.dll
        )

    set(release_src_dir "${ARCH_PREBUILT_DIRS_RELEASE}")
    set(release_files
        openjp2.dll
        )

    # Filenames are different for 32/64 bit BugSplat file and we don't
    # have any control over them so need to branch.
    if (TARGET al::sentry)
      list(APPEND release_files sentry.dll)
    endif ()

    if (TARGET ll::fmodstudio)
      list(APPEND debug_files fmodL.dll)
      list(APPEND release_files fmod.dll)
    endif ()

    if (TARGET ll::openal)
      list(APPEND debug_files OpenAL32.dll alut.dll)
      list(APPEND release_files OpenAL32.dll alut.dll)
    endif ()

    if(TARGET al::discord-gamesdk)
      list(APPEND release_files discord_game_sdk.dll)
    endif()
elseif(DARWIN)
    set(vivox_lib_dir "${ARCH_PREBUILT_DIRS_RELEASE}")
    set(slvoice_files SLVoice)
    set(vivox_libs
        libortp.dylib
        libvivoxsdk.dylib
       )
    set(debug_src_dir "${ARCH_PREBUILT_DIRS_DEBUG}")
    set(debug_files
       )
    set(release_src_dir "${ARCH_PREBUILT_DIRS_RELEASE}")
    set(release_files
        libndofdev.dylib
       )

    if (TARGET ll::fmodstudio)
      list(APPEND debug_files libfmodL.dylib)
      list(APPEND release_files libfmod.dylib)
    endif ()

    if(TARGET al::discord-gamesdk)
      list(APPEND release_files discord_game_sdk.dylib)
    endif()

elseif(LINUX)
    set(vivox_lib_dir "${ARCH_PREBUILT_DIRS_RELEASE}")
    set(vivox_libs
        libsndfile.so.1
        libortp.so
        libvivoxoal.so.1
        libvivoxsdk.so
        )
    set(slvoice_files SLVoice)

    # *TODO - update this to use LIBS_PREBUILT_DIR and LL_ARCH_DIR variables
    # or ARCH_PREBUILT_DIRS
    set(debug_src_dir "${ARCH_PREBUILT_DIRS_DEBUG}")
    set(debug_files
       )
    # *TODO - update this to use LIBS_PREBUILT_DIR and LL_ARCH_DIR variables
    # or ARCH_PREBUILT_DIRS
    set(release_src_dir "${ARCH_PREBUILT_DIRS_RELEASE}")
    # *FIX - figure out what to do with duplicate libalut.so here -brad
    set(release_files
            ${EXPAT_COPY}
            )

     if( USE_AUTOBUILD_3P )
         list( APPEND release_files
                 libjpeg.so
                 libjpeg.so.8
                 libjpeg.so.8.2.2
                 )
     endif()

    if (TARGET ll::fmodstudio)
      list(APPEND debug_files libfmodL.so)
      list(APPEND release_files libfmod.so)
    endif ()

    if(TARGET al::discord-gamesdk)
      list(APPEND release_files libdiscord_game_sdk.so)
    endif()

else(WINDOWS)
    message(STATUS "WARNING: unrecognized platform for staging 3rd party libs, skipping...")
    set(vivox_lib_dir "${CMAKE_SOURCE_DIR}/newview/vivox-runtime/i686-linux")
    set(vivox_libs "")
    # *TODO - update this to use LIBS_PREBUILT_DIR and LL_ARCH_DIR variables
    # or ARCH_PREBUILT_DIRS
    set(debug_src_dir "${CMAKE_SOURCE_DIR}/../libraries/i686-linux/lib/debug")
    set(debug_files "")
    # *TODO - update this to use LIBS_PREBUILT_DIR and LL_ARCH_DIR variables
    # or ARCH_PREBUILT_DIRS
    set(release_src_dir "${CMAKE_SOURCE_DIR}/../libraries/i686-linux/lib/release")
    set(release_files "")

    set(debug_llkdu_src "")
    set(debug_llkdu_dst "")
    set(release_llkdu_src "")
    set(release_llkdu_dst "")
    set(relwithdebinfo_llkdu_dst "")
endif(WINDOWS)


################################################################
# Done building the file lists, now set up the copy commands.
################################################################

# Curiously, slvoice_files are only copied to SHARED_LIB_STAGING_DIR_RELEASE.
# It's unclear whether this is oversight or intentional, but anyway leave the
# single copy_if_different command rather than using to_staging_dirs.

if( slvoice_src_dir )
    copy_if_different(
            ${slvoice_src_dir}
            "${SHARED_LIB_STAGING_DIR}"
            out_targets
            ${slvoice_files}
    )
    list(APPEND third_party_targets ${out_targets})
endif()

to_staging_dirs(
    ${vivox_lib_dir}
    third_party_targets
    ${vivox_libs}
    )

to_staging_dirs(
    ${release_src_dir}
    third_party_targets
    ${release_files}
    )

add_custom_target(
        stage_third_party_libs ALL
        DEPENDS ${third_party_targets}
)
