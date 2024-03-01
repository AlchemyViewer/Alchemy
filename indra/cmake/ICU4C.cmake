# -*- cmake -*-
include(Prebuilt)

include_guard()

add_library( ll::icu4c INTERFACE IMPORTED )

use_system_binary(icu4c)
use_prebuilt_binary(icu4c)
if (WINDOWS)
  target_link_libraries( ll::icu4c INTERFACE  icuuc)
elseif(DARWIN)
  target_link_libraries( ll::icu4c INTERFACE  ${ARCH_PREBUILT_DIRS_RELEASE}/libicudata.a ${ARCH_PREBUILT_DIRS_RELEASE}/libicuuc.a)
elseif(LINUX)
  target_link_libraries( ll::icu4c INTERFACE  ${ARCH_PREBUILT_DIRS_RELEASE}/libicudata.a ${ARCH_PREBUILT_DIRS_RELEASE}/libicuuc.a)
else()
  message(FATAL_ERROR "Invalid platform")
endif()

target_include_directories( ll::icu4c SYSTEM INTERFACE  ${LIBS_PREBUILT_DIR}/include/unicode )

use_prebuilt_binary(dictionaries)
