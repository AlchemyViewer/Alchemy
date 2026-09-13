# -*- cmake -*-
#
# What every alchemy triplet decides the same way. A triplet file names the
# architecture, the system and the tier, then includes this.
#
# vcpkg hashes the triplet file, not the files it includes: an edit here or
# in AlchemyTarget.cmake alone leaves every triplet's hash unchanged, and a
# binary cache then hands back ports built with the old settings. So this
# file carries a revision, every triplet states the revision it was written
# against, and the two must agree. Editing either file means bumping both,
# which changes every triplet's hash and rebuilds the ports.
set(ALCHEMY_TRIPLET_BASE_REVISION 1)
if(NOT ALCHEMY_TRIPLET_REVISION EQUAL ALCHEMY_TRIPLET_BASE_REVISION)
  message(FATAL_ERROR "This triplet was written against alchemy-base.cmake revision "
    "${ALCHEMY_TRIPLET_REVISION}, which is now ${ALCHEMY_TRIPLET_BASE_REVISION}: "
    "set ALCHEMY_TRIPLET_REVISION to ${ALCHEMY_TRIPLET_BASE_REVISION} in every triplet so their hashes change.")
endif()
if(NOT ALCHEMY_ISA_TIER MATCHES "^(baseline|v2|v3|v4)$")
  message(FATAL_ERROR "A triplet must set ALCHEMY_ISA_TIER to baseline, v2, v3 or v4")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../AlchemyTarget.cmake")

# Everything static, linked into the viewer, but for the LGPL ports: they
# link dynamically on every platform, so the libraries stay replaceable.
set(VCPKG_LIBRARY_LINKAGE static)
if(PORT MATCHES "^(hunspell|openal-soft)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

# libwebrtc has no usable Debug build; the viewer compiles without voice in
# Debug on the platforms where that matters.
if(PORT STREQUAL "webrtc")
  set(VCPKG_BUILD_TYPE release)
endif()

if(NOT DEFINED VCPKG_CMAKE_SYSTEM_NAME)
  # Windows. The CRT is static, as the viewer's is. MSVC reports the C++
  # standard it implements only when asked.
  set(VCPKG_CRT_LINKAGE static)
  al_isa_flags(${ALCHEMY_ISA_TIER} Windows ${VCPKG_TARGET_ARCHITECTURE} isa_flags)
  set(VCPKG_C_FLAGS "${isa_flags}")
  set(VCPKG_CXX_FLAGS "${isa_flags} /std:c++20 /Zc:__cplusplus")
else()
  set(VCPKG_CRT_LINKAGE dynamic)
  al_isa_flags(${ALCHEMY_ISA_TIER} ${VCPKG_CMAKE_SYSTEM_NAME} ${VCPKG_TARGET_ARCHITECTURE} isa_flags)
  # Hidden visibility, as the viewer builds with, so a static port linked
  # into a shared library exports nothing it did not mark for export. Line
  # tables in the optimised ports, so a crash through a port frame has a
  # line number on every platform; the strip and dSYM steps take them back
  # out of what ships.
  set(VCPKG_C_FLAGS "${isa_flags} -fvisibility=hidden")
  set(VCPKG_CXX_FLAGS "${isa_flags} -fvisibility=hidden -fvisibility-inlines-hidden")
  set(VCPKG_C_FLAGS_RELEASE "-g1")
  set(VCPKG_CXX_FLAGS_RELEASE "-g1")
  if(VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(VCPKG_OSX_DEPLOYMENT_TARGET ${AL_MACOS_DEPLOYMENT_TARGET})
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
      set(VCPKG_OSX_ARCHITECTURES arm64)
    else()
      set(VCPKG_OSX_ARCHITECTURES x86_64)
    endif()
  endif()
endif()
string(STRIP "${VCPKG_C_FLAGS}" VCPKG_C_FLAGS)
string(STRIP "${VCPKG_CXX_FLAGS}" VCPKG_CXX_FLAGS)
