# -*- cmake -*-

include_guard()

if (USE_FMODSTUDIO)
  add_library(ll::fmodstudio INTERFACE IMPORTED )
  target_compile_definitions(ll::fmodstudio INTERFACE LL_FMODSTUDIO=1)

  if (FMODSTUDIO_LIBRARY AND FMODSTUDIO_INCLUDE_DIR)
    # If the path have been specified in the arguments, use that

    target_link_libraries(ll::fmodstudio INTERFACE ${FMODSTUDIO_LIBRARY})
    target_include_directories(ll::fmodstudio SYSTEM INTERFACE  ${FMODSTUDIO_INCLUDE_DIR})
  else ()
    unset(FMOD_LIBRARY_RELEASE CACHE)
    unset(FMOD_LIBRARY_DEBUG CACHE)
    unset(FMOD_INCLUDE_DIR CACHE)

    if (NOT FMODSTUDIO_SDK_DIR AND WINDOWS)
      GET_FILENAME_COMPONENT(REG_DIR [HKEY_CURRENT_USER\\Software\\FMOD\ Studio\ API\ Windows] ABSOLUTE)
      set(FMODSTUDIO_SDK_DIR ${REG_DIR} CACHE PATH "Path to the FMOD Studio SDK." FORCE)
    endif ()

    if(FMODSTUDIO_SDK_DIR)
      if(WINDOWS)
        set(fmod_lib_paths "${FMODSTUDIO_SDK_DIR}/api/core/lib/x64")
      elseif(LINUX)
        set(fmod_lib_paths
          "${FMODSTUDIO_SDK_DIR}/api/core/lib/x86_64"
          "${FMODSTUDIO_SDK_DIR}/api/core/lib/x64")
      endif()
      set(fmod_inc_paths "${FMODSTUDIO_SDK_DIR}/api/core/inc")

      if(WINDOWS)
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

      find_path(FMOD_INCLUDE_DIR fmod.hpp ${fmod_inc_paths})
      if(NOT FMOD_LIBRARY_RELEASE AND NOT FMOD_INCLUDE_DIR)
        message(FATAL_ERROR "Provided FMODSTUDIO_SDK_DIR path not found '{$FMODSTUDIO_SDK_DIR}'")
      else()
        message(STATUS "Using system-provided FMOD Studio Libraries")
      endif()
    endif ()

    target_link_libraries(ll::fmodstudio INTERFACE debug ${FMOD_LINK_LIBRARY_DEBUG} optimized ${FMOD_LINK_LIBRARY_RELEASE})

    target_include_directories(ll::fmodstudio SYSTEM INTERFACE ${FMOD_INCLUDE_DIR})
  endif ()
endif ()

