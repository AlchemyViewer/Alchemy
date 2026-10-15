# -*- cmake -*-
#
# What a sanitizer triplet adds to alchemy-base.cmake. The triplet names its
# sanitizer in ALCHEMY_SANITIZER -- asan for a tree built with AddressSanitizer
# (and UndefinedBehaviorSanitizer beside it), tsan for ThreadSanitizer -- and
# includes this after alchemy-base.cmake. BootstrapVcpkg.cmake picks the
# triplet from AL_SANITIZERS.
#
# vcpkg hashes the triplet and not this file, so this file carries a revision
# of its own as alchemy-base.cmake does: editing it means bumping
# ALCHEMY_SANITIZER_BASE_REVISION and ALCHEMY_SANITIZER_REVISION in every
# sanitizer triplet, which leaves the release triplets' hashes alone.
#
# Boost.Context switches the coroutines' stacks. Its default implementation,
# fcontext, switches in assembly no sanitizer is told of, and ASan and TSan
# then take one fiber's stack for another's. ucontext tells them of each
# switch, under BOOST_USE_ASAN or BOOST_USE_TSAN. Those change the layout of
# Boost.Context's types, so every port is compiled with the define,
# boost-context is built for ucontext, and the viewer is compiled with the
# same (00-Common.cmake).
#
# TSan sees only the accesses and atomics compiled for it: a lock taken inside
# an uninstrumented library, such as Boost.Fiber's scheduler, hides the order
# it makes, and TSan reports races across it. So the tsan ports are built with
# -fsanitize=thread. ASan's interceptors see what a library does through libc
# without it, so the asan ports are not instrumented.
#
# The ports are release builds in either, whatever the viewer's configuration,
# with frame pointers, so the sanitizers' unwinder walks through their frames.

set(ALCHEMY_SANITIZER_BASE_REVISION 1)
if(NOT ALCHEMY_SANITIZER_REVISION EQUAL ALCHEMY_SANITIZER_BASE_REVISION)
  message(
    FATAL_ERROR
    "This triplet was written against alchemy-sanitizer.cmake revision "
    "${ALCHEMY_SANITIZER_REVISION}, which is now ${ALCHEMY_SANITIZER_BASE_REVISION}: "
    "set ALCHEMY_SANITIZER_REVISION to ${ALCHEMY_SANITIZER_BASE_REVISION} in every sanitizer triplet so their hashes change."
  )
endif()
if(NOT VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "The sanitizer triplets are Linux's")
endif()

set(VCPKG_BUILD_TYPE release)

if(ALCHEMY_SANITIZER STREQUAL "asan")
  set(sanitizer_flags "-DBOOST_USE_ASAN")
  set(sanitizer_link_flags "")
elseif(ALCHEMY_SANITIZER STREQUAL "tsan")
  set(sanitizer_flags "-fsanitize=thread -DBOOST_USE_TSAN")
  set(sanitizer_link_flags "-fsanitize=thread")
else()
  message(FATAL_ERROR "A sanitizer triplet must set ALCHEMY_SANITIZER to asan or tsan")
endif()
string(APPEND VCPKG_C_FLAGS " -fno-omit-frame-pointer ${sanitizer_flags}")
string(APPEND VCPKG_CXX_FLAGS " -fno-omit-frame-pointer ${sanitizer_flags}")
string(APPEND VCPKG_LINKER_FLAGS " ${sanitizer_link_flags}")
string(STRIP "${VCPKG_C_FLAGS}" VCPKG_C_FLAGS)
string(STRIP "${VCPKG_CXX_FLAGS}" VCPKG_CXX_FLAGS)
string(STRIP "${VCPKG_LINKER_FLAGS}" VCPKG_LINKER_FLAGS)

# Boost.Context's CMake build takes its implementation from this, and gives
# its consumers BOOST_USE_UCONTEXT through Boost::context.
if(PORT STREQUAL "boost-context")
  list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DBOOST_CONTEXT_IMPLEMENTATION=ucontext)
endif()
