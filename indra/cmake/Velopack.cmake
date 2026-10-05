# -*- cmake -*-
#
# The Velopack update client: velopack_libc, Velopack's C API, built from
# rust/ by Corrosion over the velopack crate. A DLL on Windows, which the
# install carries beside the viewer with its other runtime DLLs; a static
# library on macOS, which needs nothing the standard library does not. The
# release build serves every configuration.
include_guard()
add_library(ll::velopack INTERFACE IMPORTED)

if(AL_USE_VELOPACK)
  # Corrosion takes the host's target outside Windows, and a Mac builds
  # either architecture; with none named, it builds the host's. A target
  # rustup lacks is added, as for any crate.
  if(DARWIN AND CMAKE_OSX_ARCHITECTURES)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
      set(Rust_CARGO_TARGET x86_64-apple-darwin)
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      set(Rust_CARGO_TARGET aarch64-apple-darwin)
    else()
      message(FATAL_ERROR "Velopack builds for one architecture, not '${CMAKE_OSX_ARCHITECTURES}'")
    endif()
  endif()
  set(Rust_RUSTUP_INSTALL_MISSING_TARGET ON CACHE BOOL "Install a missing Rust target with rustup")
  find_package(Corrosion CONFIG REQUIRED)

  if(WINDOWS)
    set(velopack_crate_type cdylib)
  else()
    set(velopack_crate_type staticlib)
  endif()
  corrosion_import_crate(
    MANIFEST_PATH "${INDRA_SOURCE_DIR}/rust/Cargo.toml"
    CRATES velopack_libc
    CRATE_TYPES ${velopack_crate_type}
    PROFILE release
    LOCKED
  )
  # Built for the viewer that links it, not by every build with Velopack on.
  set_target_properties(cargo-build_velopack_libc PROPERTIES EXCLUDE_FROM_ALL TRUE)
  if(WINDOWS AND CMAKE_LINKER)
    # rustc runs link.exe by name, and a build from Git Bash finds Git's
    # coreutils link first on the path. cargo takes the one CMake found, for
    # the crate and for the build scripts it builds for the same target.
    string(TOUPPER "${Rust_CARGO_TARGET}" velopack_rust_target)
    string(REPLACE "-" "_" velopack_rust_target "${velopack_rust_target}")
    corrosion_set_env_vars(velopack_libc "CARGO_TARGET_${velopack_rust_target}_LINKER=${CMAKE_LINKER}")
  endif()
  if(DARWIN)
    # What Corrosion links a static library with on macOS, System, c and m,
    # is libSystem, which every link takes already: given again, ld warns of
    # the duplicate. Nor is its CommandLineTools SDK directory wanted, which
    # a Mac with only Xcode does not have.
    set_target_properties(
      velopack_libc-static
      PROPERTIES INTERFACE_LINK_LIBRARIES "" INTERFACE_LINK_DIRECTORIES ""
    )
  endif()

  target_compile_definitions(ll::velopack INTERFACE LL_VELOPACK=1)
  target_include_directories(
    ll::velopack
    SYSTEM
    INTERFACE "${INDRA_SOURCE_DIR}/rust/velopack_libc/include"
  )
  target_link_libraries(ll::velopack INTERFACE velopack_libc)
endif()
