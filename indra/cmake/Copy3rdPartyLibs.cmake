# -*- cmake -*-

# The copy_win_libs folder contains file lists and a script used to
# copy dlls, exes and such needed to run the SecondLife from within
# VisualStudio.

include(CMakeCopyIfDifferent)
include(Linking)
include(DiscordSDK)
include(FMODSTUDIO)
include(Sentry)

# When we copy our dependent libraries, we almost always want to copy them to
# both the Release and the RelWithDebInfo staging directories. This has
# resulted in duplicate (or worse, erroneous attempted duplicate)
# copy_if_different commands. Encapsulate that usage.
# Pass FROM_DIR, TARGETS and the files to copy. TO_DIR is implicit.
# to_staging_dirs diverges from copy_if_different in that it appends to TARGETS.
MACRO(to_debug_staging_dirs from_dir targets)
  foreach(staging_dir
          "${SHARED_LIB_STAGING_DIR_DEBUG}")
    copy_if_different("${from_dir}" "${staging_dir}" out_targets ${ARGN})
    list(APPEND "${targets}" "${out_targets}")
  endforeach()
ENDMACRO(to_debug_staging_dirs from_dir to_dir targets)

MACRO(to_relwithdeb_staging_dirs from_dir targets)
  foreach(staging_dir
          "${SHARED_LIB_STAGING_DIR_RELWITHDEBINFO}")
    copy_if_different("${from_dir}" "${staging_dir}" out_targets ${ARGN})
    list(APPEND "${targets}" "${out_targets}")
  endforeach()
ENDMACRO(to_relwithdeb_staging_dirs from_dir to_dir targets)

MACRO(to_release_staging_dirs from_dir targets)
  foreach(staging_dir
          "${SHARED_LIB_STAGING_DIR_RELEASE}")
    copy_if_different("${from_dir}" "${staging_dir}" out_targets ${ARGN})
    list(APPEND "${targets}" "${out_targets}")
  endforeach()
ENDMACRO(to_release_staging_dirs from_dir to_dir targets)

###################################################################
# set up platform specific lists of files that need to be copied
###################################################################
if(WINDOWS)
    if(GEN_IS_MULTI_CONFIG)
        set(SHARED_LIB_STAGING_DIR_DEBUG            "${SHARED_LIB_STAGING_DIR}/Debug")
        set(SHARED_LIB_STAGING_DIR_RELWITHDEBINFO   "${SHARED_LIB_STAGING_DIR}/RelWithDebInfo")
        set(SHARED_LIB_STAGING_DIR_RELEASE          "${SHARED_LIB_STAGING_DIR}/Release")
    elseif (UPPERCASE_CMAKE_BUILD_TYPE MATCHES DEBUG)
        set(SHARED_LIB_STAGING_DIR_DEBUG            "${SHARED_LIB_STAGING_DIR}")
    elseif (UPPERCASE_CMAKE_BUILD_TYPE MATCHES RELWITHDEBINFO)
        set(SHARED_LIB_STAGING_DIR_RELWITHDEBINFO   "${SHARED_LIB_STAGING_DIR}")
    elseif (UPPERCASE_CMAKE_BUILD_TYPE MATCHES RELEASE)
        set(SHARED_LIB_STAGING_DIR_RELEASE          "${SHARED_LIB_STAGING_DIR}")
    endif()

    #*******************************
    # VIVOX - *NOTE: no debug version
    set(vivox_lib_dir "${ARCH_PREBUILT_DIRS_RELEASE}")
    set(slvoice_src_dir "${ARCH_PREBUILT_BIN_DIRS_RELEASE}")    
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
    if (USE_SENTRY)
      list(APPEND release_files sentry.dll)
    endif ()

    if (USE_FMODSTUDIO)
      list(APPEND debug_files fmodL.dll)
      list(APPEND release_files fmod.dll)
    endif (USE_FMODSTUDIO)

    if (USE_OPENAL)
      list(APPEND debug_files OpenAL32.dll alut.dll)
      list(APPEND release_files OpenAL32.dll alut.dll)
    endif ()

    if (USE_KDU)
      list(APPEND debug_files kdud.dll)
      list(APPEND release_files kdu.dll)
    endif (USE_KDU)

    if(USE_DISCORD)
      list(APPEND release_files discord_game_sdk.dll)
    endif()
    
    #*******************************
    # Copy MS C runtime dlls, required for packaging.
    if (MSVC80)
        set(MSVC_VER 80)
    elseif (MSVC_VERSION EQUAL 1600) # VisualStudio 2010
        MESSAGE(STATUS "MSVC_VERSION ${MSVC_VERSION}")
    elseif (MSVC_VERSION EQUAL 1800) # VisualStudio 2013, which is (sigh) VS 12
        set(MSVC_VER 120)
    elseif (MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1940) # Visual Studio 2017 through 2022
        set(MSVC_VER 140)
    elseif (MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS 1930) # Visual Studio 2019
        set(MSVC_VER 140)
    elseif (MSVC_VERSION GREATER_EQUAL 1930 AND MSVC_VERSION LESS 1940) # Visual Studio 2019
        set(MSVC_VER 140)        
    else (MSVC80)
        MESSAGE(WARNING "New MSVC_VERSION ${MSVC_VERSION} of MSVC: adapt Copy3rdPartyLibs.cmake")
    endif (MSVC80)

    if(ADDRESS_SIZE EQUAL 32)
        # this folder contains the 32bit DLLs.. (yes really!)
        set(registry_find_path "[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Windows;Directory]/SysWOW64")
    else(ADDRESS_SIZE EQUAL 32)
        # this folder contains the 64bit DLLs.. (yes really!)
        set(registry_find_path "[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Windows;Directory]/System32")
    endif(ADDRESS_SIZE EQUAL 32)

    # Having a string containing the system registry path is a start, but to
    # get CMake to actually read the registry, we must engage some other
    # operation.
    get_filename_component(registry_path "${registry_find_path}" ABSOLUTE)

    # These are candidate DLL names. Empirically, VS versions before 2015 have
    # msvcp*.dll and msvcr*.dll. VS 2017 has msvcp*.dll and vcruntime*.dll.
    # Check each of them.
    foreach(release_msvc_file
            concrt${MSVC_VER}.dll
            msvcp${MSVC_VER}.dll
            msvcp${MSVC_VER}_1.dll
            msvcp${MSVC_VER}_2.dll
            msvcp${MSVC_VER}_atomic_wait.dll
            msvcp${MSVC_VER}_codecvt_ids.dll
            vccorlib${MSVC_VER}.dll
            vcruntime${MSVC_VER}.dll
            vcruntime${MSVC_VER}_1.dll
            )
        if(EXISTS "${registry_path}/${release_msvc_file}")
            to_relwithdeb_staging_dirs(
                ${registry_path}
                third_party_targets
                ${release_msvc_file})
            to_release_staging_dirs(
                ${registry_path}
                third_party_targets
                ${release_msvc_file})
        else()
            # This isn't a WARNING because, as noted above, every VS version
            # we've observed has only a subset of the specified DLL names.
            MESSAGE(STATUS "Redist lib ${release_msvc_file} not found")
        endif()
    endforeach()
    MESSAGE(STATUS "Will copy redist files for MSVC ${MSVC_VER}:")
    foreach(target ${third_party_targets})
        MESSAGE(STATUS "${target}")
    endforeach()

elseif(DARWIN)
    set(SHARED_LIB_STAGING_DIR_DEBUG            "${SHARED_LIB_STAGING_DIR}/Debug/Resources")
    set(SHARED_LIB_STAGING_DIR_RELWITHDEBINFO   "${SHARED_LIB_STAGING_DIR}/RelWithDebInfo/Resources")
    set(SHARED_LIB_STAGING_DIR_RELEASE          "${SHARED_LIB_STAGING_DIR}/Release/Resources")

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

    if (USE_FMODSTUDIO)
      list(APPEND debug_files libfmodL.dylib)
      list(APPEND release_files libfmod.dylib)
    endif (USE_FMODSTUDIO)

    if(USE_DISCORD)
      list(APPEND release_files discord_game_sdk.dylib)
    endif()

elseif(LINUX)
    # linux is weird, multiple side by side configurations aren't supported
    # and we don't seem to have any debug shared libs built yet anyways...
    set(SHARED_LIB_STAGING_DIR_DEBUG            "${SHARED_LIB_STAGING_DIR}")
    set(SHARED_LIB_STAGING_DIR_RELWITHDEBINFO   "${SHARED_LIB_STAGING_DIR}")
    set(SHARED_LIB_STAGING_DIR_RELEASE          "${SHARED_LIB_STAGING_DIR}")

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
        libopenal.so
        libjpeg.so
        libjpeg.so.8
        libjpeg.so.8.2.2
       )

    if (USE_SENTRY)
      list(APPEND release_files libsentry.so)
    endif ()

    if (USE_FMODSTUDIO)
      list(APPEND debug_files libfmodL.so)
      list(APPEND release_files libfmod.so)
    endif (USE_FMODSTUDIO)

    if(USE_DISCORD)
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

if (GEN_IS_MULTI_CONFIG OR UPPERCASE_CMAKE_BUILD_TYPE MATCHES DEBUG)
    copy_if_different(
        ${slvoice_src_dir}
        "${SHARED_LIB_STAGING_DIR_DEBUG}"
        out_targets
        ${slvoice_files}
        )
    list(APPEND third_party_targets ${out_targets})

    to_debug_staging_dirs(
        ${vivox_lib_dir}
        third_party_targets
        ${vivox_libs}
        )

    to_debug_staging_dirs(
        ${debug_src_dir}
        third_party_targets
        ${debug_files}
        )
endif()

if (GEN_IS_MULTI_CONFIG OR UPPERCASE_CMAKE_BUILD_TYPE MATCHES RELWITHDEBINFO)
    copy_if_different(
        ${slvoice_src_dir}
        "${SHARED_LIB_STAGING_DIR_RELWITHDEBINFO}"
        out_targets
        ${slvoice_files}
        )
    list(APPEND third_party_targets ${out_targets})

    to_relwithdeb_staging_dirs(
        ${vivox_lib_dir}
        third_party_targets
        ${vivox_libs}
        )

    to_relwithdeb_staging_dirs(
        ${release_src_dir}
        third_party_targets
        ${release_files}
        )
endif()

if (GEN_IS_MULTI_CONFIG OR UPPERCASE_CMAKE_BUILD_TYPE MATCHES RELEASE)
    copy_if_different(
        ${slvoice_src_dir}
        "${SHARED_LIB_STAGING_DIR_RELEASE}"
        out_targets
        ${slvoice_files}
        )
    list(APPEND third_party_targets ${out_targets})

    to_release_staging_dirs(
        ${vivox_lib_dir}
        third_party_targets
        ${vivox_libs}
        )

    to_release_staging_dirs(
        ${release_src_dir}
        third_party_targets
        ${release_files}
        )
endif()

if(NOT USESYSTEMLIBS)
  add_custom_target(
      stage_third_party_libs ALL
      DEPENDS ${third_party_targets}
      )
endif(NOT USESYSTEMLIBS)
