# -*- cmake -*-
include(Prebuilt)

set(LIBXML2_FIND_QUIETLY ON)
set(LIBXML2_FIND_REQUIRED ON)

if(USESYSTEMLIBS)
  include(FindLibXml2)
else(USESYSTEMLIBS)
    use_prebuilt_binary(libxml2)
    if(WINDOWS)
        set(LIBXML2_LIBRARIES
            debug ${ARCH_PREBUILT_DIRS_DEBUG}/libxml2_a.lib
            optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libxml2_a.lib
        )
    elseif(DARWIN)
        set(LIBXML2_LIBRARIES xml2 iconv)
    else()
        set(LIBXML2_LIBRARIES xml2)
    endif()
    set(LIBXML2_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/libxml2)
endif(USESYSTEMLIBS)
