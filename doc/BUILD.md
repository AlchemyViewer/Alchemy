# Building Alchemy Viewer

This is the shared reference for building Alchemy: presets, configuration types, options, tests, and packaging. For OS-specific setup (compilers, SDKs, system packages), see the platform guides:

- [Windows](BUILD.WINDOWS.md)
- [Linux](BUILD.LINUX.md)
- [macOS](BUILD.MAC.md)

## Prerequisites

All platforms need the following, in addition to a C++ toolchain. Platform-specific install commands live in the platform guides.

- **CMake** 3.27 or newer
- **Git**
- **Python** 3.13+ — used for build-time scripts. Install dependencies into a venv:
  ```
  python3 -m venv .venv
  # Windows: .\.venv\Scripts\Activate.ps1
  # Unix:    source .venv/bin/activate
  pip install -r requirements.txt
  ```
- **Rust** — required by Velopack, which is used to produce installer packages. Only needed if you intend to package (i.e. `PACKAGE=ON`, the default).
- **.NET SDK** — also for Velopack. After installation run `dotnet tool restore` in the repo root to fetch the `vpk` tool.

If you are not producing installers, you can disable packaging with `-DPACKAGE=OFF` at configure time and skip the Rust and .NET setup.

## Presets

Build configuration is driven by CMake presets defined in [`indra/CMakePresets.json`](../indra/CMakePresets.json). A preset selects the generator (Visual Studio, Ninja, Xcode), the target architecture, and whether proprietary components are enabled.

List all available presets:

```
cmake -S indra --list-presets
```

### Naming convention

Preset names follow the pattern `<generator>[-<arch>][-os]`:

- **`-os` suffix** — open-source only. Excludes proprietary components (KDU JPEG2000 codec, FMOD audio, and other non-free libraries).
- **No `-os` suffix** — sets `INSTALL_PROPRIETARY=ON`. Requires licensed source for the proprietary components and is only useful if you have them.

Most contributors want the `-os` variants.

### Common presets

| Preset                                                                          | Platform | Generator                 |
|:--------------------------------------------------------------------------------|:---------|:--------------------------|
| `vs2026-os`, `vs2022-os`                                                        | Windows  | Visual Studio             |
| `ninja-os`                                                                      | Linux    | Ninja Multi-Config        |
| `ninja-os-arm64`, `ninja-os-x64`                                                | macOS    | Ninja Multi-Config        |
| `xcode-os`, `xcode-os-arm64`, `xcode-os-x64`                                    | macOS    | Xcode                     |

Configure with:

```
cmake -S indra --preset <preset-name>
```

This creates a build tree at `build-<HostSystem>-<preset>/` next to the repo (e.g. `build-Windows-vs2026-os/`, `build-Linux-ninja-os/`, `build-Darwin-xcode-os/`).

### Workflow presets (one-shot configure + build)

Workflow presets run configure and build as a single command. Useful for CI and one-off release builds:

```
cmake --workflow --preset ninja-os-release
cmake --workflow --preset vs2026-os-release
cmake --workflow --preset xcode-os-release
```

See `workflowPresets` in `indra/CMakePresets.json` for the full set.

## Configuration types

Ninja and Xcode presets are multi-config; Visual Studio presets always are. Pick a configuration at build time with `--config <Config>` (or `--preset <build-preset>` where the config is baked in).

| Configuration    | Libraries | Asserts | Notes                                              |
|:-----------------|:----------|:--------|:---------------------------------------------------|
| `Debug`          | debug     | yes     | Slowest; full debugging of viewer and deps         |
| `OptDebug`       | release   | yes     | Optimized libs with debuggable viewer code         |
| `RelWithDebInfo` | release   | yes     | Default for Ninja presets; ship-adjacent with asserts |
| `Release`        | release   | no      | Ship builds                                        |

## Build options

Override any option at configure time with `-D<NAME>:<TYPE>=<VALUE>`. For example:

```
cmake -S indra --preset ninja-os -DBUILD_TESTING=ON -DUSE_FMODSTUDIO=ON
```

Options are defined in [`indra/CMakeLists.txt`](../indra/CMakeLists.txt). The ones you are most likely to touch:

### Build targets

| Option                  | Default | Description                                                         |
|:------------------------|:--------|:--------------------------------------------------------------------|
| `BUILD_VIEWER`          | ON      | Build the viewer executable                                         |
| `BUILD_APPEARANCE_UTIL` | OFF     | Build the appearance utility                                        |
| `BUILD_TESTING`         | OFF     | Build and run unit + integration tests                              |
| `PACKAGE`               | ON      | Produce installer packages after the viewer build (requires Velopack) |
| `USE_VELOPACK`          | OFF     | Use Velopack for installer packaging (instead of NSIS/DMG)          |

### Audio

| Option           | Default | Description                            |
|:-----------------|:--------|:---------------------------------------|
| `USE_OPENAL`     | ON      | OpenAL audio engine                    |
| `USE_FMODSTUDIO` | OFF     | FMOD Studio audio engine (proprietary) |

### Profiling

| Option                 | Default            | Description                                  |
|:-----------------------|:-------------------|:---------------------------------------------|
| `USE_TRACY`            | ON for test builds | Tracy profiler support                       |
| `USE_TRACY_ON_DEMAND`  | ON                 | Only profile when a Tracy server connects    |
| `USE_TRACY_LOCAL_ONLY` | ON                 | Disallow remote Tracy profiling              |
| `USE_TRACY_GPU`        | OFF                | Tracy GPU profiling                          |

### Optimization / instrumentation

| Option                                                       | Default | Description                                        |
|:-------------------------------------------------------------|:--------|:---------------------------------------------------|
| `USE_LTO`                                                    | OFF     | Link Time Optimization                             |
| `USE_SSE4_2`, `USE_AVX`, `USE_AVX2`                          | OFF     | Target SIMD instruction sets (x86_64 only)         |
| `ENABLE_ASAN`, `ENABLE_UBSAN`, `ENABLE_THREADSAN`            | OFF     | Sanitizers (macOS and Linux only)                  |
| `VS_DISABLE_FATAL_WARNINGS` / `GCC_DISABLE_FATAL_WARNINGS` / `CLANG_DISABLE_FATAL_WARNINGS` | OFF | Don't treat warnings as errors (useful when a new compiler introduces new diagnostics) |
| `DISABLE_RELEASE_DEBUG_LOGGING`                              | varies  | Strip debug-level logging from Release builds       |

### Media plugins

| Option                  | Default | Description                                  |
|:------------------------|:--------|:---------------------------------------------|
| `BUILD_CEF_PLUGIN`      | ON      | Chromium Embedded Framework (in-world web)   |
| `BUILD_VLC_PLUGIN`      | ON      | VLC media plugin                             |
| `BUILD_GSTREAMER_PLUGIN` | ON on Linux | GStreamer media plugin (Linux only)     |
| `BUILD_EXAMPLE_PLUGIN`  | ON      | Reference/example plugin                     |

### Platform-specific

| Option            | Default       | Description                                                |
|:------------------|:--------------|:-----------------------------------------------------------|
| `USE_NVAPI`       | ON            | NVIDIA NVAPI for GPU profile support (Windows only)        |
| `USE_OPENXR`      | OFF           | OpenXR VR support (experimental)                           |
| `USE_SDL_WINDOW`  | ON on Linux   | SDL-based window management (Linux only; Wayland path)     |

### Crash reporting

| Option                        | Default | Description                                  |
|:------------------------------|:--------|:---------------------------------------------|
| `USE_SENTRY`                  | OFF     | Sentry crash reporting                       |
| `RELEASE_CRASH_REPORTING`     | OFF     | Enable crash reporting in Release builds     |
| `NON_RELEASE_CRASH_REPORTING` | OFF     | Enable crash reporting in developer builds   |

See [`indra/CMakeLists.txt`](../indra/CMakeLists.txt) for the complete list.

## Building

After configuring, build with CMake or your IDE. From the command line:

```
# multi-config generators (VS, Xcode, Ninja Multi-Config)
cmake --build <build-dir> --config Release

# or use a build preset
cmake --build --preset ninja-os-release
```

Output locations are platform-specific; see the platform guides.

## Running tests

Enable tests at configure time:

```
cmake -S indra --preset <preset> -DBUILD_TESTING=ON
```

Build and run with CTest:

```
cmake --build <build-dir> --config RelWithDebInfo
ctest --test-dir <build-dir> --output-on-failure
```

Unit tests live alongside the library they cover in `indra/<library>/tests/`. The framework is TUT (Template Unit Test). Integration tests are in `indra/integration_tests/`.

## Packaging

Release packages are produced by [Velopack](https://velopack.io). The packaging step runs automatically after a successful build when `PACKAGE=ON` (the default). To skip it during development:

```
cmake -S indra --preset <preset> -DPACKAGE=OFF
```

Velopack also requires `dotnet tool restore` to have been run so the `vpk` CLI is on PATH.

## See also

- [Troubleshooting](TROUBLESHOOTING.md) — common build failures and their fixes
- [Contributing](../CONTRIBUTING.md)
