# Build Troubleshooting

Common failures building Alchemy Viewer, and what to do about them.

## Configure

### First `cmake -S indra --preset ...` takes forever

Expected on the first run: vcpkg downloads and builds every C/C++ dependency from source. Budget **30–60+ minutes** and several GB of disk. Subsequent configures reuse the vcpkg cache and finish in seconds.

If the run produces no output for a very long time, it usually isn't hung — check CPU/disk activity before killing it.

### CMake is too old

Alchemy requires CMake 3.27+. If your distro ships something older, install a newer version via pip inside your venv:

```
pip install --upgrade cmake ninja
```

### `vpk` command not found (or packaging step fails)

Velopack requires the `vpk` .NET tool. Install it once per clone:

```
dotnet tool restore
```

If you don't intend to produce installers, disable packaging entirely:

```
cmake -S indra --preset <preset> -DPACKAGE=OFF
```

and skip the Rust / .NET setup.

### Rust / `cargo` missing during packaging

Packaging uses Velopack, which invokes `cargo`. Install a stable Rust toolchain:

```
rustup default stable
```

Again, this is only needed if `PACKAGE=ON` (the default). See [BUILD.md](BUILD.md#packaging).

## Build

### Warnings fail the build

By default, warnings are treated as errors. New compiler releases sometimes introduce diagnostics the tree doesn't yet clean up. Disable fatal warnings for the affected toolchain at configure time:

```
# MSVC / Visual Studio
cmake -S indra --preset vs2026-os -DVS_DISABLE_FATAL_WARNINGS=TRUE

# GCC
cmake -S indra --preset ninja-os -DGCC_DISABLE_FATAL_WARNINGS=TRUE

# Clang
cmake -S indra --preset ninja-os -DCLANG_DISABLE_FATAL_WARNINGS=TRUE
```

### Visual Studio doesn't recognise `Alchemy.slnx`

`.slnx` is the newer Visual Studio solution format. Use Visual Studio 2022 17.10+ or Visual Studio 2026, or configure with the `vs2022-os` preset on an older compatible edition.

### `cmake --build --preset ninja-os-release` fails with "no such preset"

You probably configured with a proprietary preset (e.g. `ninja`, no `-os` suffix). Build presets are tied to configure presets; use the matching build preset for whichever configure preset you used (for example `ninja-release` for `ninja`).

## Platform-specific

### Linux: missing system headers during vcpkg builds

Double-check the package list for your distro in [BUILD.LINUX.md](BUILD.LINUX.md). Common offenders when a package lookup produces an error like `<something>.h not found`:

- `autoconf-archive` — required by several vcpkg ports
- `libxkbcommon-dev`, `libwayland-dev`, `wayland-protocols` — required for SDL window and Wayland support
- `libgstreamer-plugins-base1.0-dev` — required for the GStreamer media plugin

### macOS: wrong architecture produced

`xcode-os` and `ninja-os` default to the host architecture. If you want to cross-build (e.g. an arm64 bundle from an Intel Mac), use the explicit `-arm64` / `-x64` preset. See the table in [BUILD.MAC.md](BUILD.MAC.md#3-configure).

## Still stuck?

- Ask on the [Discord](https://discordapp.com/invite/KugCgs6).
- File a build bug at <https://github.com/AlchemyViewer/Alchemy/issues>.
