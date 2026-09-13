# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Alchemy Viewer is a third-party client for Second Life, forked from the official Linden Lab viewer. It is a large C++ desktop application (~750 source files in the main viewer module alone) using OpenGL for rendering, with builds targeting Windows, macOS, and Linux.

## Build System

CMake with vcpkg for dependency management. The source root for CMake is `indra/` (not the repo root). All CMake presets are defined in `indra/CMakePresets.json`.

### Prerequisites

- CMake 4.0+, Visual Studio 2022/2026 (Windows) or Xcode (macOS) or GCC/Clang+Ninja (Linux)
- Rust and the .NET SDK (`dotnet tool restore`) only for Velopack installers
- Python 3 with `llsd` (`pip install -r requirements.txt`) only for the four tests that spawn a Python peer; without it they are registered disabled

CMake files are formatted with gersemi (`.gersemirc` at the repo root): run `gersemi -i <files>` on any CMake file you edit.

### Configure (first time or after CMake changes)

```
# Windows (Visual Studio 2026)
cmake -S indra --preset vs2026-os

# Linux (Ninja)
cmake -S indra --preset ninja-os

# macOS (Xcode)
cmake -S indra --preset xcode-os
```

Append `-os` presets for open-source builds, omit `-os` for proprietary builds (adds `-DAL_ENABLE_PROPRIETARY=ON`). List available presets: `cmake -S indra --list-presets`

### Build

```
# Windows
cmake --build build-Windows-vs2026-os --config Release

# Linux
cmake --build --preset ninja-os-release

# macOS
cmake --build build-Darwin-xcode-os --config Release
```

Configuration types: `Debug`, `OptDebug` (debug build, release libs), `RelWithDebInfo` (default), `Release`.

The viewer executable lands at `build-<OS>-<preset>/newview/<CONFIG>/` (e.g., `SecondLifeViewer.exe` on Windows, `SecondLife.app` on macOS).

### Tests

Tests are off by default. To enable: add `-DAL_BUILD_TESTS=ON` to the configure command. Then run via CTest:

```
ctest --test-dir build-<OS>-<preset> --output-on-failure
```

Unit tests live alongside the library they test in `indra/<library>/tests/` directories. The test framework is TUT (Template Unit Test). Test targets are defined by the `LL_ADD_PROJECT_UNIT_TESTS` macro in `indra/cmake/LLAddBuildTest.cmake`. Integration tests are in `indra/integration_tests/`.

## Architecture

### Source Tree (`indra/`)

All source code lives under `indra/`. The codebase is organized as a set of libraries that the main viewer application (`newview`) links against. Dependency flows downward — libraries only depend on libraries listed above them:


### Architecture breakdown
- @doc/ARCHITECTURE.md