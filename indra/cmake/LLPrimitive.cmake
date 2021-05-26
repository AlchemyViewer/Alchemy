# -*- cmake -*-

# these should be moved to their own cmake file
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
        debug libcollada14dom23-sd
        optimized libcollada14dom23-s
        ${BOOST_FILESYSTEM_LIBRARY}
        ${BOOST_SYSTEM_LIBRARIES}
        libxml2_a
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

