# -*- cmake -*-
#
# The machine a build targets: the instruction set as compiler flags, the
# level the source reads it back as, and the oldest macOS it runs on. Read by
# the root CMakeLists and 00-Common.cmake for the viewer and by
# triplets/alchemy-base.cmake for the ports, so both are built for the same
# machine. A change here is a change to every triplet: bump the revision in
# triplets/alchemy-base.cmake.
#
# AL_ISA_TIER names an x86-64 microarchitecture level: baseline, v2, v3
# (AVX2) or v4 (AVX-512). arm64 has no tiers: ARMv8.0-A NEON already carries
# fused multiply-add, IEEE min and max, rounding and the 16-bit conversions,
# and every Mac past the macOS floor is an M1 or later. macOS x86_64 ignores
# the tier and stays at SSE4.2: every Mac that runs macOS 14 has AVX2, but
# Rosetta 2 on that release does not translate AVX, and the x86_64 build
# exists for Rosetta.

include_guard()

set(AL_MACOS_DEPLOYMENT_TARGET 14.0)

# al_isa_flags(<tier> <system> <architecture> <result>)
#   system is Windows, Linux or Darwin; architecture is x64, x86_64 or arm64.
#   Leaves <result> empty where the tier adds nothing to the compiler's
#   default.
function(al_isa_flags tier system architecture result)
  set(flags "")
  if(architecture MATCHES "^(x64|x86_64|AMD64)$")
    if(system STREQUAL "Darwin")
      set(flags -msse4.2)
    elseif(system STREQUAL "Windows")
      if(tier STREQUAL "v4")
        set(flags /arch:AVX512)
      elseif(tier STREQUAL "v3")
        set(flags /arch:AVX2)
      elseif(tier STREQUAL "v2")
        set(flags /arch:SSE4.2)
      endif()
    else()
      if(tier STREQUAL "baseline")
        set(flags -march=x86-64)
      else()
        set(flags -march=x86-64-${tier})
      endif()
    endif()
  elseif(architecture MATCHES "^(arm64|aarch64|ARM64)$")
    # The architecture's floor, named so the build says what it targets.
    # Apple's compiler targets the M1 by default and Windows on ARM builds
    # for ARMv8.0 by default; neither takes a -march.
    if(system STREQUAL "Linux")
      set(flags -march=armv8-a)
    endif()
  endif()
  set(${result} "${flags}" PARENT_SCOPE)
endfunction()

# al_isa_level(<tier> <system> <architecture> <result>)
#   The level the source reads the tier back as, through AL_ISA_LEVEL: 0 for
#   the baseline, 2 for SSE4.2, 3 for AVX2, 4 for AVX-512. It exists because
#   MSVC accepts /arch:SSE4.2 and defines nothing for it, so the flag alone
#   cannot be read back; alsimd.h checks that the level and the flags agree.
#   macOS x86_64 is 2 whatever the tier, as its flags are. arm64 is 0: NEON is
#   the architecture's own and has no level.
function(al_isa_level tier system architecture result)
  set(level 0)
  if(architecture MATCHES "^(x64|x86_64|AMD64)$")
    if(system STREQUAL "Darwin")
      set(level 2)
    elseif(tier STREQUAL "v4")
      set(level 4)
    elseif(tier STREQUAL "v3")
      set(level 3)
    elseif(tier STREQUAL "v2")
      set(level 2)
    endif()
  endif()
  set(${result} ${level} PARENT_SCOPE)
endfunction()

# al_isa_triplet_tier(<tier> <system> <architecture> <result>)
#   The suffix of the vcpkg triplet that carries the tier: the ports are
#   built per tier on x86-64 Windows and Linux, and at the baseline elsewhere
#   or below v3.
function(al_isa_triplet_tier tier system architecture result)
  set(suffix "")
  if(system MATCHES "^(Windows|Linux)$" AND architecture MATCHES "^(x64|x86_64|AMD64)$")
    if(tier STREQUAL "v4")
      set(suffix "-avx512")
    elseif(tier STREQUAL "v3")
      set(suffix "-avx2")
    endif()
  endif()
  set(${result} "${suffix}" PARENT_SCOPE)
endfunction()
