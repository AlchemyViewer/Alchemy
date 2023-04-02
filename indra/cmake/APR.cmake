include(Linking)
include(Prebuilt)

include_guard()

add_library( ll::apr INTERFACE IMPORTED )

use_system_binary( apr apr-util )
use_prebuilt_binary(apr_suite)

if (WINDOWS)
  set(APR_selector "")
  target_link_libraries( ll::apr INTERFACE
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/${APR_selector}apr-1.lib
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}apr-1.lib
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/${APR_selector}apriconv-1.lib
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}apriconv-1.lib
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/${APR_selector}aprutil-1.lib
	  optimized ${ARCH_PREBUILT_DIRS_RELEASE}/${APR_selector}aprutil-1.lib
          )
elseif (DARWIN)
  target_link_libraries( ll::apr INTERFACE
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/libapr-1.a
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libapr-1.a
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/libaprutil-1.a
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libaprutil-1.a
          iconv
          )

else()
  target_link_libraries( ll::apr INTERFACE
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/libapr-1.a
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libapr-1.a
          debug ${ARCH_PREBUILT_DIRS_DEBUG}/libaprutil-1.a
          optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libaprutil-1.a
          iconv
          rt
          )
endif ()
target_include_directories( ll::apr SYSTEM INTERFACE  ${LIBS_PREBUILT_DIR}/include/apr-1 )
