# -*- cmake -*-
#
# pnj2k, the Rust JPEG 2000 codec: its C API, built by Corrosion as a static
# library on every platform, the release build for every configuration, for
# llimagej2crs to decode and encode textures with. The C++ runtime the
# viewer selects serves the library too: Corrosion leaves it to the
# executable.
include_guard()
add_library(ll::pnj2k INTERFACE IMPORTED)

if(AL_USE_PNJ2K)
  # The Cargo workspace that builds the C API, the crate's directory in it,
  # and its name: rust/pnj2k_libc, a workspace of its own, which takes
  # pnj2k-capi from pnj2k's repository at the revision its Cargo.lock pins
  # and carries that revision's pnj2k.h; or, where PNJ2K_SOURCE_DIR names a
  # checkout of pnj2k, the checkout's own pnj2k-capi and header.
  if(PNJ2K_SOURCE_DIR)
    set(pnj2k_crate "${PNJ2K_SOURCE_DIR}" crates/pnj2k-capi pnj2k-capi)
  else()
    set(pnj2k_crate "${INDRA_SOURCE_DIR}/rust/pnj2k_libc" . pnj2k_libc)
  endif()
  list(GET pnj2k_crate 0 pnj2k_workspace)
  list(GET pnj2k_crate 1 pnj2k_dir)
  list(GET pnj2k_crate 2 pnj2k_package)
  set(pnj2k_manifest "${pnj2k_workspace}/Cargo.toml")
  set(pnj2k_include "${pnj2k_workspace}/${pnj2k_dir}/include")
  string(REPLACE "-" "_" pnj2k_target "${pnj2k_package}")
  if(NOT EXISTS "${pnj2k_manifest}")
    message(
      FATAL_ERROR
      "PNJ2K_SOURCE_DIR: '${pnj2k_manifest}' does not exist; name a checkout of pnj2k, or leave it empty for the pinned revision"
    )
  endif()

  # As Velopack.cmake: Corrosion takes the host's target outside Windows, a
  # Mac builds either architecture, and a target rustup lacks is added.
  if(DARWIN AND CMAKE_OSX_ARCHITECTURES)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
      set(Rust_CARGO_TARGET x86_64-apple-darwin)
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      set(Rust_CARGO_TARGET aarch64-apple-darwin)
    else()
      message(FATAL_ERROR "pnj2k builds for one architecture, not '${CMAKE_OSX_ARCHITECTURES}'")
    endif()
  endif()
  set(Rust_RUSTUP_INSTALL_MISSING_TARGET ON CACHE BOOL "Install a missing Rust target with rustup")
  find_package(Corrosion CONFIG REQUIRED)

  corrosion_import_crate(
    MANIFEST_PATH "${pnj2k_manifest}"
    CRATES ${pnj2k_package}
    CRATE_TYPES staticlib
    PROFILE release
    LOCKED
  )
  # Built for the viewer that links it, not by every build with pnj2k on.
  set_target_properties(cargo-build_${pnj2k_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)

  # The tree's instruction set, as AlchemyTarget.cmake gives the C++ its:
  # macOS x86_64 at SSE4.2 whatever the tier, arm64 at its baseline. The
  # codec picks its vector kernels at run time; this is for the code
  # between them.
  if(ARCH MATCHES "^(x64|x86_64|AMD64)$")
    if(DARWIN)
      corrosion_add_target_rustflags(${pnj2k_target} -Ctarget-feature=+sse4.2)
    elseif(AL_ISA_TIER MATCHES "^v[234]$")
      corrosion_add_target_rustflags(${pnj2k_target} -Ctarget-cpu=x86-64-${AL_ISA_TIER})
    endif()
  endif()

  # Nothing of how the codec is written leaves in the library, whichever
  # source it is built from and whatever a profile or cargo's configuration
  # asks: no debug information, whose line tables name the sources and
  # which a symbol upload would take them by; no file, line or column in a
  # panic's location; and symbols named by a hash, not by their path
  # through the crate. The last two are rustc's -Z options, which a stable
  # toolchain takes under RUSTC_BOOTSTRAP.
  corrosion_set_env_vars(${pnj2k_target} RUSTC_BOOTSTRAP=1)
  corrosion_add_target_rustflags(
    ${pnj2k_target}
    -Cdebuginfo=0
    -Zlocation-detail=none
    -Zunstable-options
    -Csymbol-mangling-version=hashed
  )

  # No LTO, whatever the workspace's profile asks, a checkout's included. A
  # Rust static library carries the standard library, and one link takes two
  # of them, this and Velopack's, on macOS and Linux. Without LTO each holds
  # the standard library's own objects, the same in both, so the linker
  # takes them from whichever it reads first and finds nothing more to take
  # from the other. LTO compiles the standard library again into objects of
  # the crate's own, which define what the other library's define, and the
  # link stops on duplicate symbols.
  corrosion_set_env_vars(${pnj2k_target} CARGO_PROFILE_RELEASE_LTO=off)

  # Each new build of the library is checked for what it must not hold; see
  # Pnj2kAudit.cmake. llimagej2crs, through which everything links the
  # library, depends on the check. The library's own target cannot: the
  # check names that target's file, and CMake 4.0 and 4.1 take the two for
  # a cycle.
  set(pnj2k_library "$<TARGET_FILE:${pnj2k_target}-static>")
  set(pnj2k_audit_stamp "${CMAKE_BINARY_DIR}/pnj2k-audit.stamp")
  add_custom_command(
    OUTPUT "${pnj2k_audit_stamp}"
    COMMAND
      ${CMAKE_COMMAND} "-DLIBRARY=${pnj2k_library}" "-DSTAMP=${pnj2k_audit_stamp}" -P
      "${CMAKE_CURRENT_LIST_DIR}/Pnj2kAudit.cmake"
    DEPENDS "${pnj2k_library}" "${CMAKE_CURRENT_LIST_DIR}/Pnj2kAudit.cmake"
    COMMENT "Checking that pnj2k's library gives nothing of its sources away"
    VERBATIM
  )
  add_custom_target(pnj2k-audit DEPENDS "${pnj2k_audit_stamp}")
  set_target_properties(pnj2k-audit PROPERTIES EXCLUDE_FROM_ALL TRUE)
  add_dependencies(pnj2k-audit cargo-build_${pnj2k_target})

  if(WINDOWS AND CMAKE_LINKER)
    # As Velopack.cmake: rustc runs link.exe by name for the build scripts
    # and proc macros, and a build from Git Bash finds Git's coreutils link
    # first on the path.
    string(TOUPPER "${Rust_CARGO_TARGET}" pnj2k_rust_target)
    string(REPLACE "-" "_" pnj2k_rust_target "${pnj2k_rust_target}")
    corrosion_set_env_vars(${pnj2k_target} "CARGO_TARGET_${pnj2k_rust_target}_LINKER=${CMAKE_LINKER}")
  endif()
  if(DARWIN)
    # As Velopack.cmake: libSystem, which every link takes already, given
    # again makes ld warn.
    set_target_properties(
      ${pnj2k_target}-static
      PROPERTIES INTERFACE_LINK_LIBRARIES "" INTERFACE_LINK_DIRECTORIES ""
    )
  endif()

  target_include_directories(ll::pnj2k SYSTEM INTERFACE "${pnj2k_include}")
  target_link_libraries(ll::pnj2k INTERFACE ${pnj2k_target})
endif()
