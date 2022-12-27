# -*- cmake -*-
include(Prebuilt)

set(EXPAT_FIND_QUIETLY ON)
set(EXPAT_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindEXPAT)
else (USESYSTEMLIBS)
    use_prebuilt_binary(expat)
    if (WINDOWS)
        set(EXPAT_LIBRARIES
            debug ${ARCH_PREBUILT_DIRS_DEBUG}/libexpatd.lib
            optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libexpat.lib)
        set(EXPAT_COPY libexpat.dll)
    elseif (DARWIN)
        set(EXPAT_LIBRARIES
            debug ${ARCH_PREBUILT_DIRS_DEBUG}/libexpat.a
            optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libexpat.a)
    else ()
        set(EXPAT_LIBRARIES expat)
        set(EXPAT_COPY libexpat.so.1 libexpat.so)
    endif ()
    set(EXPAT_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
endif (USESYSTEMLIBS)
