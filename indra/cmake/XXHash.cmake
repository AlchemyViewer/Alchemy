# -*- cmake -*-

include_guard()

include(Prebuilt)

add_library( ll::xxhash INTERFACE IMPORTED )

use_system_binary( xxhash )

use_prebuilt_binary(xxhash)  
# if(WINDOWS)
#   target_link_libraries( ll::xxhash INTERFACE
#     debug ${ARCH_PREBUILT_DIRS_DEBUG}/xxhash.lib
#     optimized ${ARCH_PREBUILT_DIRS_RELEASE}/xxhash.lib)
# else()
#   target_link_libraries( ll::xxhash INTERFACE xxhash)
# endif()
  
  target_include_directories( ll::xxhash SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/xxhash)
