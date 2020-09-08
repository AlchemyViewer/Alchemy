# -*- cmake -*-

# these should be moved to their own cmake file
include(Prebuilt)
include(Boost)
include(URIPARSER)

use_prebuilt_binary(colladadom)
use_prebuilt_binary(libxml2)

set(LLPRIMITIVE_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llprimitive
    )
if (WINDOWS)
    set(LLPRIMITIVE_LIBRARIES 
        debug llprimitive
        optimized llprimitive
        debug libcollada14dom23-d
        optimized libcollada14dom23
        ${BOOST_SYSTEM_LIBRARIES}
        )
elseif (DARWIN)
    use_prebuilt_binary(pcre)
    set(LLPRIMITIVE_LIBRARIES 
        llprimitive
        debug collada14dom-d
        optimized collada14dom
        minizip
        xml2
        pcrecpp
        pcre
        iconv           # Required by libxml2
        )
elseif (LINUX)
    set(LLPRIMITIVE_LIBRARIES 
        llprimitive
        debug collada14dom-d
        optimized collada14dom
        minizip
        xml2
        ${URIPARSER_LIBRARIES}
        )
endif (WINDOWS)

