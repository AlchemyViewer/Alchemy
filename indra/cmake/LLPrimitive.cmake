# -*- cmake -*-

# these should be moved to their own cmake file
include(Linking)
include(Prebuilt)
include(Boost)
include(URIPARSER)
include(ZLIB)

use_prebuilt_binary(colladadom)
use_prebuilt_binary(libxml2)
use_prebuilt_binary(minizip-ng)

set(LLPRIMITIVE_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llprimitive
    )
if (WINDOWS)
    set(LLPRIMITIVE_LIBRARIES 
        debug llprimitive
        optimized llprimitive
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcollada14dom23-sd.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcollada14dom23-s.lib
        ${BOOST_FILESYSTEM_LIBRARY}
        ${BOOST_SYSTEM_LIBRARIES}
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libxml2_a.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libxml2_a.lib
        ${URIPARSER_LIBRARIES}
        ${MINIZIP_LIBRARIES}
        ${ZLIB_LIBRARIES}
        )
elseif (DARWIN)
    set(LLPRIMITIVE_LIBRARIES 
        llprimitive
        debug collada14dom-d
        optimized collada14dom
        ${BOOST_FILESYSTEM_LIBRARY}
        ${BOOST_SYSTEM_LIBRARIES}
        xml2
        iconv           # Required by libxml2
        ${URIPARSER_LIBRARIES}
        ${MINIZIP_LIBRARIES}
        ${ZLIB_LIBRARIES}
        )
elseif (LINUX)
    set(LLPRIMITIVE_LIBRARIES 
        llprimitive
        debug collada14dom-d
        optimized collada14dom
        ${BOOST_FILESYSTEM_LIBRARY}
        ${BOOST_SYSTEM_LIBRARIES}
        xml2
        ${URIPARSER_LIBRARIES}
        ${MINIZIP_LIBRARIES}
        ${ZLIB_LIBRARIES}
        )
endif (WINDOWS)

