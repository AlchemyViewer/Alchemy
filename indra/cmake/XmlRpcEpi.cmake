# -*- cmake -*-
include(Prebuilt)

set(XMLRPCEPI_FIND_QUIETLY ON)
set(XMLRPCEPI_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindXmlRpcEpi)
else (USESYSTEMLIBS)
    use_prebuilt_binary(xmlrpc-epi)
    if (WINDOWS)
        set(XMLRPCEPI_LIBRARIES
            debug ${ARCH_PREBUILT_DIRS_DEBUG}/xmlrpc-epid.lib
            optimized ${ARCH_PREBUILT_DIRS_RELEASE}/xmlrpc-epi.lib
        )
    elseif (DARWIN)
        set(XMLRPCEPI_LIBRARIES
            debug ${ARCH_PREBUILT_DIRS_DEBUG}/libxmlrpc-epi.a
            optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libxmlrpc-epi.a
        )
    else()
        set(XMLRPCEPI_LIBRARIES xmlrpc-epi)
    endif (WINDOWS)
    set(XMLRPCEPI_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
endif (USESYSTEMLIBS)
