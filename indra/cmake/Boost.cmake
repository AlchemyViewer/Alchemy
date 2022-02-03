# -*- cmake -*-
include(Prebuilt)
include(Linking)

set(Boost_FIND_QUIETLY ON)
set(Boost_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindBoost)

  set(BOOST_CONTEXT_LIBRARY boost_context-mt)
  set(BOOST_FIBER_LIBRARY boost_fiber-mt)
  set(BOOST_FILESYSTEM_LIBRARY boost_filesystem-mt)
  set(BOOST_PROGRAM_OPTIONS_LIBRARY boost_program_options-mt)
  set(BOOST_REGEX_LIBRARY boost_regex-mt)
  set(BOOST_SIGNALS_LIBRARY boost_signals-mt)
  set(BOOST_SYSTEM_LIBRARY boost_system-mt)
  set(BOOST_THREAD_LIBRARY boost_thread-mt)
else (USESYSTEMLIBS)
  use_prebuilt_binary(boost)
  set(Boost_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)

  # As of sometime between Boost 1.67 and 1.72, Boost libraries are suffixed
  # with the address size.
  set(addrsfx "-x${ADDRESS_SIZE}")

  if (WINDOWS)
    set(BOOST_CONTEXT_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_context-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_context-mt-gd${addrsfx}.lib)
    set(BOOST_FIBER_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_fiber-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_fiber-mt-gd${addrsfx}.lib)
    set(BOOST_FILESYSTEM_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_filesystem-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_filesystem-mt-gd${addrsfx}.lib)
    set(BOOST_PROGRAM_OPTIONS_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_program_options-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_program_options-mt-gd${addrsfx}.lib)
    set(BOOST_REGEX_LIBRARY
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_regex-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_regex-mt-gd${addrsfx}.lib)
    set(BOOST_SIGNALS_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_signals-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_signals-mt-gd${addrsfx}.lib)
    set(BOOST_STACKTRACE_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_stacktrace_windbg-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_stacktrace_windbg-mt-gd${addrsfx}.lib)
    set(BOOST_SYSTEM_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_system-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_system-mt-gd${addrsfx}.lib)
    set(BOOST_THREAD_LIBRARY 
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libboost_thread-mt${addrsfx}.lib
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libboost_thread-mt-gd${addrsfx}.lib)
  elseif (LINUX)
    set(BOOST_CONTEXT_LIBRARY
        optimized boost_context-mt${addrsfx}
        debug boost_context-mt${addrsfx}-d)
    set(BOOST_FIBER_LIBRARY
        optimized boost_fiber-mt${addrsfx}
        debug boost_fiber-mt${addrsfx}-d)
    set(BOOST_FILESYSTEM_LIBRARY
        optimized boost_filesystem-mt${addrsfx}
        debug boost_filesystem-mt${addrsfx}-d)
    set(BOOST_PROGRAM_OPTIONS_LIBRARY
        optimized boost_program_options-mt${addrsfx}
        debug boost_program_options-mt${addrsfx}-d)
    set(BOOST_REGEX_LIBRARY
        optimized boost_regex-mt${addrsfx}
        debug boost_regex-mt${addrsfx}-d)
    set(BOOST_SIGNALS_LIBRARY
        optimized boost_signals-mt${addrsfx}
        debug boost_signals-mt${addrsfx}-d)
    set(BOOST_SYSTEM_LIBRARY
        optimized boost_system-mt${addrsfx}
        debug boost_system-mt${addrsfx}-d)
    set(BOOST_THREAD_LIBRARY
        optimized boost_thread-mt${addrsfx}
        debug boost_thread-mt${addrsfx}-d)
  elseif (DARWIN)
    set(BOOST_CONTEXT_LIBRARY
        optimized boost_context-mt
        debug boost_context-mt-d)
    set(BOOST_FIBER_LIBRARY
        optimized boost_fiber-mt
        debug boost_fiber-mt-d)
    set(BOOST_FILESYSTEM_LIBRARY
        optimized boost_filesystem-mt
        debug boost_filesystem-mt-d)
    set(BOOST_PROGRAM_OPTIONS_LIBRARY
        optimized boost_program_options-mt
        debug boost_program_options-mt-d)
    set(BOOST_REGEX_LIBRARY
        optimized boost_regex-mt
        debug boost_regex-mt-d)
    set(BOOST_SIGNALS_LIBRARY
        optimized boost_signals-mt
        debug boost_signals-mt-d)
    set(BOOST_SYSTEM_LIBRARY
        optimized boost_system-mt
        debug boost_system-mt-d)
    set(BOOST_THREAD_LIBRARY
        optimized boost_thread-mt
        debug boost_thread-mt-d)
  endif (WINDOWS)
endif (USESYSTEMLIBS)

if (LINUX)
    set(BOOST_SYSTEM_LIBRARY ${BOOST_SYSTEM_LIBRARY} rt)
    set(BOOST_THREAD_LIBRARY ${BOOST_THREAD_LIBRARY} rt)
endif (LINUX)

