# -*- cmake -*-

include_guard()

include(Prebuilt)

add_library( ll::uriparser INTERFACE IMPORTED )

use_system_binary( uriparser )

use_prebuilt_binary(uriparser)
if (WINDOWS)
    target_compile_definitions( ll::uriparser INTERFACE URI_STATIC_BUILD=1)
    target_link_libraries( ll::uriparser INTERFACE
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/uriparser.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/uriparser.lib)
elseif (LINUX)
    target_link_libraries( ll::uriparser INTERFACE 
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/liburiparser.a
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/liburiparser.a)
elseif (DARWIN)
    target_link_libraries( ll::uriparser INTERFACE liburiparser.dylib)
endif (WINDOWS)
target_include_directories( ll::uriparser SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/uriparser)
