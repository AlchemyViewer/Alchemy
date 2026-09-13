# -*- cmake -*-
#
# FMOD Studio is downloaded from fmod.com under its own licence, so it is not
# a port: AL_FMODSTUDIO_SDK_DIR names the installed SDK, or AL_FMODSTUDIO_LIBRARY
# and AL_FMODSTUDIO_INCLUDE_DIR name the pieces directly. On Windows the
# installer records the SDK location in the registry, and that is the default.
include_guard()
add_library(ll::fmodstudio INTERFACE IMPORTED)

if(AL_USE_FMODSTUDIO)
  target_compile_definitions(ll::fmodstudio INTERFACE LL_FMODSTUDIO=1)

  if(AL_FMODSTUDIO_LIBRARY AND AL_FMODSTUDIO_INCLUDE_DIR)
    target_link_libraries(ll::fmodstudio INTERFACE ${AL_FMODSTUDIO_LIBRARY})
    target_include_directories(ll::fmodstudio SYSTEM INTERFACE ${AL_FMODSTUDIO_INCLUDE_DIR})
  else()
    unset(FMOD_LIBRARY_RELEASE CACHE)
    unset(FMOD_LIBRARY_DEBUG CACHE)
    unset(FMOD_INCLUDE_DIR CACHE)

    if(NOT AL_FMODSTUDIO_SDK_DIR AND WINDOWS)
      cmake_host_system_information(
        RESULT registered_sdk_dir
        QUERY WINDOWS_REGISTRY "HKCU/Software/FMOD Studio API Windows"
        ERROR_VARIABLE registry_error
      )
      if(NOT registry_error)
        set(
          AL_FMODSTUDIO_SDK_DIR
          "${registered_sdk_dir}"
          CACHE PATH
          "Path to the FMOD Studio SDK."
          FORCE
        )
      endif()
    endif()

    if(NOT AL_FMODSTUDIO_SDK_DIR)
      message(FATAL_ERROR "AL_USE_FMODSTUDIO needs the SDK: -DAL_FMODSTUDIO_SDK_DIR=<path>")
    endif()

    if(DARWIN)
      set(fmod_lib_paths "${AL_FMODSTUDIO_SDK_DIR}/api/core/lib")
    else()
      set(fmod_lib_paths "${AL_FMODSTUDIO_SDK_DIR}/api/core/lib/x64")
    endif()
    set(fmod_inc_paths "${AL_FMODSTUDIO_SDK_DIR}/api/core/inc")

    if(WINDOWS)
      # The SDK ships fmod.dll beside fmod_vc.lib; the DLL is what the
      # staging step copies, the import library is what links.
      set(CMAKE_FIND_LIBRARY_SUFFIXES_OLD ${CMAKE_FIND_LIBRARY_SUFFIXES})
      set(CMAKE_FIND_LIBRARY_SUFFIXES .dll)
    endif()

    find_library(FMOD_LIBRARY_RELEASE fmod PATHS ${fmod_lib_paths} NO_DEFAULT_PATH REQUIRED)
    find_library(FMOD_LIBRARY_DEBUG fmodL PATHS ${fmod_lib_paths} NO_DEFAULT_PATH REQUIRED)

    if(WINDOWS)
      set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES_OLD})
      string(REPLACE ".dll" "_vc.lib" FMOD_LINK_LIBRARY_RELEASE ${FMOD_LIBRARY_RELEASE})
      string(REPLACE ".dll" "_vc.lib" FMOD_LINK_LIBRARY_DEBUG ${FMOD_LIBRARY_DEBUG})
    else()
      set(FMOD_LINK_LIBRARY_RELEASE ${FMOD_LIBRARY_RELEASE})
      set(FMOD_LINK_LIBRARY_DEBUG ${FMOD_LIBRARY_DEBUG})
    endif()

    find_path(FMOD_INCLUDE_DIR fmod.hpp PATHS ${fmod_inc_paths} NO_DEFAULT_PATH REQUIRED)
    message(VERBOSE "Using FMOD Studio from ${AL_FMODSTUDIO_SDK_DIR}")

    target_link_libraries(
      ll::fmodstudio
      INTERFACE debug ${FMOD_LINK_LIBRARY_DEBUG} optimized ${FMOD_LINK_LIBRARY_RELEASE}
    )
    target_include_directories(ll::fmodstudio SYSTEM INTERFACE ${FMOD_INCLUDE_DIR})
  endif()
endif()
