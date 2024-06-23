# -*- cmake -*-
include(Prebuilt)

include_guard()

add_library( ll::boost INTERFACE IMPORTED )
if( USE_CONAN )
  target_link_libraries( ll::boost INTERFACE CONAN_PKG::boost )
  target_compile_definitions( ll::boost INTERFACE BOOST_ALLOW_DEPRECATED_HEADERS BOOST_BIND_GLOBAL_PLACEHOLDERS )
  return()
endif()

use_prebuilt_binary(boost)

# As of sometime between Boost 1.67 and 1.72, Boost libraries are suffixed
# with the address size.
set(addrsfx "-x${ADDRESS_SIZE}")

if (WINDOWS)
  target_link_libraries( ll::boost INTERFACE
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_context-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_context-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_fiber-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_fiber-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_filesystem-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_filesystem-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_program_options-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_program_options-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_regex-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_regex-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_stacktrace_windbg-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_stacktrace_windbg-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_system-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_system-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_thread-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_thread-mt-gd${addrsfx}.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_wave-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_wave-mt-gd${addrsfx}.lib
    )
elseif (LINUX)
  target_link_libraries( ll::boost INTERFACE
        ${ARCH_PREBUILT_DIRS}/libboost_fiber-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_context-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_filesystem-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_program_options-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_regex-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_thread-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_wave-mt${addrsfx}.a
        ${ARCH_PREBUILT_DIRS}/libboost_system-mt${addrsfx}.a
        rt
    )
elseif (DARWIN)
  target_link_libraries( ll::boost INTERFACE
        optimized boost_context-mt
        debug boost_context-mt-d
        optimized boost_fiber-mt
        debug boost_fiber-mt-d
        optimized boost_filesystem-mt
        debug boost_filesystem-mt-d
        optimized boost_program_options-mt
        debug boost_program_options-mt-d
        optimized boost_regex-mt
        debug boost_regex-mt-d
        optimized boost_system-mt
        debug boost_system-mt-d
        optimized boost_thread-mt
        debug boost_thread-mt-d
        optimized boost_wave-mt
        debug boost_wave-mt-d
    )
endif (WINDOWS)

target_compile_definitions( ll::boost INTERFACE BOOST_ALLOW_DEPRECATED_HEADERS=1 BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1 BOOST_BIND_GLOBAL_PLACEHOLDERS=1)
