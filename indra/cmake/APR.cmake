include(Linking)
include(Prebuilt)

set(APR_FIND_QUIETLY ON)
set(APR_FIND_REQUIRED ON)

set(APRUTIL_FIND_QUIETLY ON)
set(APRUTIL_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindAPR)
else (USESYSTEMLIBS)
  use_prebuilt_binary(apr_suite)
  if (WINDOWS)
    set(APR_selector "lib")
    set(APR_LIBRARIES 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/${APR_selector}apr-1.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}apr-1.lib
      )
    set(APRICONV_LIBRARIES 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/${APR_selector}apriconv-1.lib
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}apriconv-1.lib
      )
    set(APRUTIL_LIBRARIES 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/${APR_selector}aprutil-1.lib ${APRICONV_LIBRARIES}
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}aprutil-1.lib ${APRICONV_LIBRARIES}
      )
  elseif (DARWIN)
    set(APR_LIBRARIES 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libapr-1.a
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libapr-1.a
      )
    set(APRICONV_LIBRARIES iconv)
    set(APRUTIL_LIBRARIES 
      debug ${ARCH_PREBUILT_DIRS_DEBUG}/libaprutil-1.a ${APRICONV_LIBRARIES}
      optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libaprutil-1.a ${APRICONV_LIBRARIES}
      )
  else (WINDOWS)
    set(APR_LIBRARIES apr-1)
    set(APRUTIL_LIBRARIES aprutil-1)
    set(APRICONV_LIBRARIES iconv)
  endif (WINDOWS)
  set(APR_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include/apr-1)

  if (LINUX)
    list(APPEND APRUTIL_LIBRARIES rt)
  endif (LINUX)
endif (USESYSTEMLIBS)
