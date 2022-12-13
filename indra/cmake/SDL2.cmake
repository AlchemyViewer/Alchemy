# -*- cmake -*-
if (USESYSTEMLIBS)
    include(FindPkgConfig)
    pkg_check_modules(SDL REQUIRED sdl2)
else ()
    include(Prebuilt)
    if (LINUX)
        use_prebuilt_binary(SDL2)
        set (SDL_FOUND TRUE)
        set (SDL_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/SDL2)
        if(WINDOWS)
            set (SDL_LIBRARIES 
                debug ${ARCH_PREBUILT_DIRS_DEBUG}/SDL2d.lib
                optimized ${ARCH_PREBUILT_DIRS_RELEASE}/SDL2.lib)
        else()
            set (SDL_LIBRARIES SDL2)
        endif()
    endif (LINUX)
endif ()