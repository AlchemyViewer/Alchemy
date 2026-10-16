# -*- cmake -*-
#
# ICU from vcpkg, found directly. FindICU also looks for fourteen ICU tools
# and two data files a static port does not ship, on every configure, and
# that search alone was a fifth of a reconfigure. The names per platform and
# configuration are the ones vcpkg's own wrapper for FindICU uses.
include_guard()

set(icu_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
find_path(ICU_INCLUDE_DIR unicode/uversion.h PATHS "${icu_root}/include" NO_DEFAULT_PATH REQUIRED)
find_library(
  ICU_I18N_LIBRARY_RELEASE
  NAMES icui18n icuin
  NAMES_PER_DIR
  PATHS "${icu_root}/lib"
  NO_DEFAULT_PATH
  REQUIRED
)
find_library(
  ICU_I18N_LIBRARY_DEBUG
  NAMES icui18nd icuind icui18n icuin
  NAMES_PER_DIR
  PATHS "${icu_root}/debug/lib"
  NO_DEFAULT_PATH
)
find_library(
  ICU_UC_LIBRARY_RELEASE
  NAMES icuuc
  NAMES_PER_DIR
  PATHS "${icu_root}/lib"
  NO_DEFAULT_PATH
  REQUIRED
)
find_library(
  ICU_UC_LIBRARY_DEBUG
  NAMES icuucd icuuc
  NAMES_PER_DIR
  PATHS "${icu_root}/debug/lib"
  NO_DEFAULT_PATH
)
find_library(
  ICU_DATA_LIBRARY_RELEASE
  NAMES icudata icudt
  NAMES_PER_DIR
  PATHS "${icu_root}/lib"
  NO_DEFAULT_PATH
  REQUIRED
)
find_library(
  ICU_DATA_LIBRARY_DEBUG
  NAMES icudatad icudtd icudata icudt
  NAMES_PER_DIR
  PATHS "${icu_root}/debug/lib"
  NO_DEFAULT_PATH
)
mark_as_advanced(
  ICU_INCLUDE_DIR
  ICU_I18N_LIBRARY_RELEASE
  ICU_I18N_LIBRARY_DEBUG
  ICU_UC_LIBRARY_RELEASE
  ICU_UC_LIBRARY_DEBUG
  ICU_DATA_LIBRARY_RELEASE
  ICU_DATA_LIBRARY_DEBUG
)

add_library(ll::icu INTERFACE IMPORTED)
target_include_directories(ll::icu SYSTEM INTERFACE ${ICU_INCLUDE_DIR})

# Static archives, in dependency order: i18n over uc over data. A tree from
# a -release triplet has no debug libraries and links the release ones in
# every configuration, as every other port does there.
foreach(component I18N UC DATA)
  if(ICU_${component}_LIBRARY_DEBUG)
    target_link_libraries(
      ll::icu
      INTERFACE
        optimized ${ICU_${component}_LIBRARY_RELEASE}
        debug ${ICU_${component}_LIBRARY_DEBUG}
    )
  else()
    target_link_libraries(ll::icu INTERFACE ${ICU_${component}_LIBRARY_RELEASE})
  endif()
endforeach()

# ICU's C++ API stays visible: Boost.Regex's Unicode support (boost::u32regex,
# boost/regex/icu.hpp) is written against it, including unicode/coll.h and
# using icu::Collator, icu::Locale and icu::UnicodeString. ICU's C++ ABI is
# unstable between versions, but we link a pinned ICU statically with our own
# toolchain, so there is no dynamic boundary for that to matter across.
target_compile_definitions(ll::icu INTERFACE U_SHOW_CPLUSPLUS_API=1)

# The ports are static (triplets/alchemy-base.cmake). Without this the headers
# declare every entry point __declspec(dllimport) and the link looks for
# symbols a static ICU does not carry.
target_compile_definitions(ll::icu INTERFACE U_STATIC_IMPLEMENTATION)
