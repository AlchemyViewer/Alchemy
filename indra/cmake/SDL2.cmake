# -*- cmake -*-
include(Prebuilt)

include_guard()

if(LINUX)
set(USE_SDL ON)
endif()

if(USE_SDL)

add_library( ll::SDL2 INTERFACE IMPORTED )

use_system_binary( SDL2 )

use_prebuilt_binary(SDL2)  
if(WINDOWS)
  target_link_libraries( ll::SDL2 INTERFACE
    debug ${ARCH_PREBUILT_DIRS_DEBUG}/SDL2d.lib
    optimized ${ARCH_PREBUILT_DIRS_RELEASE}/SDL2.lib)
elseif(LINUX)
  target_link_libraries( ll::SDL2 INTERFACE
    debug ${ARCH_PREBUILT_DIRS_DEBUG}/libSDL2d.so
    optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libSDL2.so)
else()
  target_link_libraries( ll::SDL2 INTERFACE SDL2)
endif()
  
target_include_directories( ll::SDL2 SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/SDL2)

target_compile_definitions( ll::SDL2 INTERFACE LL_SDL=1)

endif()