# Building Alchemy Viewer

Everything you need to build Alchemy from source: platform setup, presets, options, tests, packaging, and troubleshooting.

- [Prerequisites](#prerequisites)
- [Platform setup](#platform-setup) — Windows, macOS, Linux
- [Clone and bootstrap](#clone-and-bootstrap)
- [Configure](#configure)
- [Build](#build)
- [Configuration types](#configuration-types)
- [Build options](#build-options)
- [Running tests](#running-tests)
- [Packaging](#packaging)
- [Troubleshooting](#troubleshooting)

## Prerequisites

Every platform needs a C++ toolchain plus:

- **CMake** 4.0+
- **Git**
- **Rust** and **.NET SDK** — only for Velopack installers (`-DAL_USE_VELOPACK=ON`)
- **Python** 3 — only for the tests that spawn a Python peer (see [Running tests](#running-tests))

Install commands are platform-specific; see below.

## Platform setup

### Windows

Install the following:

- [Visual Studio 2026](https://visualstudio.microsoft.com/vs/community/) — select the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 4.0+
- [Git for Windows](https://git-scm.com/install/windows)
- [Rust](https://rust-lang.org/tools/install/) — run `rustup-init.exe` and accept defaults (packaging only)
- [.NET SDK](https://dotnet.microsoft.com/en-us/download) (packaging only)

Sanity-check in a fresh terminal:

```
cmake --version
git --version
```

### macOS

Install [Xcode](https://developer.apple.com/xcode/) from the App Store, then run `xcode-select --install` to get the command-line tools.

Install [Homebrew](https://brew.sh/), then the build dependencies:

```
brew install git cmake zip unzip curl pkgconf automake autoconf autoconf-archive \
    gettext libtool rustup dotnet
```

Initialize the Rust toolchain (packaging only):

```
rustup-init -y
```

### Linux

Install system packages for your distro:

<details>
<summary>Arch</summary>

```
sudo pacman -Syu automake autoconf autoconf-archive base-devel cmake fontconfig git glib2-devel \
    gstreamer gst-plugins-base-libs ninja libglvnd libtool libvlc libx11 pkgconf python \
    wayland dotnet-sdk rustup zip nasm
```

</details>

<details>
<summary>Debian 12+</summary>

```
sudo apt install \
    autoconf autoconf-archive automake bison build-essential cmake curl flex gettext \
    libasound2-dev libaudio-dev libdbus-1-dev libdecor-0-dev libdrm-dev \
    libegl1-mesa-dev libfribidi-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
    libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libibus-1.0-dev libjack-dev \
    libosmesa6-dev libpipewire-0.3-dev libpulse-dev libsndio-dev libtext-unidecode-perl \
    libthai-dev libtool libudev-dev libunwind-dev liburing-dev libvlc-dev libwayland-dev \
    libx11-dev libxcursor-dev libxext-dev libxfixes-dev libxft-dev libxi-dev libxinerama-dev \
    libxkbcommon-dev libxrandr-dev libxss-dev libxtst-dev linux-libc-dev ninja-build \
    pkgconf tar tex-common texinfo unzip zip dotnet-sdk-10.0 rustup nasm
```

</details>

<details open>
<summary>Ubuntu 22.04+</summary>

```
sudo apt install \
    autoconf autoconf-archive automake bison build-essential cmake curl flex gettext \
    libasound2-dev libaudio-dev libdbus-1-dev libdecor-0-dev libdrm-dev \
    libegl1-mesa-dev libfribidi-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
    libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libibus-1.0-dev libjack-dev \
    libosmesa6-dev libpipewire-0.3-dev libpulse-dev libsndio-dev libtext-unidecode-perl \
    libthai-dev libtool libudev-dev libunwind-dev liburing-dev libvlc-dev libwayland-dev \
    libx11-dev libxcursor-dev libxext-dev libxfixes-dev libxft-dev libxi-dev libxinerama-dev \
    libxkbcommon-dev libxrandr-dev libxss-dev libxtst-dev linux-libc-dev ninja-build \
    pkgconf tar tex-common texinfo unzip zip dotnet-sdk-10.0 rustup nasm
```

</details>

<details>
<summary>Fedora / RHEL</summary>

**AlmaLinux 10:**

```
sudo dnf group install "Development Tools"
sudo dnf install cmake fontconfig-devel git glib2-devel gstreamer1-devel \
    gstreamer1-plugins-base-devel libX11-devel mesa-libOSMesa-devel libglvnd-devel \
    ninja-build python3 vlc-devel wayland-devel dotnet-sdk-10.0 rustup
```

You may need to enable EPEL first: `sudo dnf install epel-release`

**Fedora 44+:**

```
sudo dnf install @development-tools @c-development cmake fontconfig-devel git glib-devel \
    gstreamer1-devel gstreamer1-plugins-base-devel libX11-devel \
    mesa-compat-libOSMesa-devel libglvnd-devel ninja-build python3 vlc-devel \
    wayland-devel dotnet-sdk-10.0 rustup perl-IPC-Cmd perl-FindBin perl-Time-Piece \
    autoconf-archive perl-open libXcursor-devel wayland-protocols-devel dbus-devel \
    ibus-devel mesa-libGLU-devel libxkbcommon-devel mesa-libEGL-devel mesa-libGL-devel \
    libXtst-devel libXrandr-devel pipewire-devel pulseaudio-libs-devel alsa-lib-devel \
    nasm libXScrnSaver-devel
```

To build with Clang instead of GCC, also install: `sudo dnf install clang lld`

</details>

<details>
<summary>OpenSUSE Tumbleweed</summary>

```
sudo zypper in -t pattern devel_basis devel_C_C++
sudo zypper install cmake fontconfig-devel git glib2-devel gstreamer-devel \
    gstreamer-plugins-base-devel libglvnd-devel libX11-devel ninja Mesa-libGL-devel \
    python3 vlc-devel wayland-devel
```

</details>

Initialize a stable Rust toolchain (packaging only):

```
rustup default stable
```

## Clone and bootstrap

Alchemy vendors the [Dullahan](https://github.com/AlchemyViewer/dullahan) CEF wrapper — used by the in-world web media plugin — as a git submodule under `indra/dullahan`. It builds from source as part of the tree, so the submodule must be present before you configure. Clone with `--recurse-submodules`:

```
git clone --recurse-submodules https://github.com/AlchemyViewer/Alchemy.git alchemy
cd alchemy
dotnet tool restore        # Velopack installers only
```

Already cloned without `--recurse-submodules`? Fetch the submodules before configuring:

```
git submodule update --init --recursive
```

After pulling upstream changes, run the same command to keep the submodule in sync with the revision the tree expects.

## Configure

Build configuration is driven by CMake presets. [`indra/CMakePresets.json`](../indra/CMakePresets.json) includes one file per generator under [`indra/cmake/presets/`](../indra/cmake/presets/) (`vs2026.json`, `ninja.json`, `xcode.json`), each of which includes `base.json`, the hidden bases they are composed from. `generate.py` beside them writes all five; edit its tables, not the JSON. A preset selects the generator (Visual Studio, Ninja, Xcode), the target architecture, and whether proprietary components are enabled.

List all available presets:

```
cmake -S indra --list-presets
```

### Naming convention

Preset names follow the pattern `<generator>[-<arch>][-os]`:

- **`-os` suffix** — open-source only. Excludes proprietary components (KDU JPEG2000 codec, FMOD audio, and other non-free libraries).
- **No `-os` suffix** — sets `AL_ENABLE_PROPRIETARY=ON`. Requires licensed source for the proprietary components and is only useful if you have access to them.

Most contributors want the `-os` variants.

`<generator>[-os]-fullopt` (with `-arm64` / `-x64` on macOS) is that preset with the optimizations of a shipped build: LTO on, Tracy and Release-configuration debug logging off. The channel is not part of it — pass `-DAL_CHANNEL=...` as for any preset — and neither is the Velopack update client (`-DAL_USE_VELOPACK=ON`), which CI adds. The Ninja ones default to the Release configuration. The hidden `fullopt` preset carries the three settings for a preset of your own, for example `{"name": "mine", "inherits": ["ninja-os", "fullopt", "mold"]}` in `CMakeUserPresets.json`.

### Common presets

| Preset                                       | Platform | Generator          |
|:---------------------------------------------|:---------|:-------------------|
| `vs2026-os`                                  | Windows  | Visual Studio      |
| `ninja-os`                                   | Linux    | Ninja Multi-Config |
| `ninja-os-arm64`, `ninja-os-x64`             | macOS    | Ninja Multi-Config |
| `xcode-os`, `xcode-os-arm64`, `xcode-os-x64` | macOS    | Xcode              |

Configure with:

```
cmake -S indra --preset <preset-name>
```

This creates a build tree at `build-<HostSystem>-<preset>/` next to the source — e.g. `build-Windows-vs2026-os/`, `build-Linux-ninja-os/`, `build-Darwin-xcode-os-arm64/`.

The first configure run downloads and builds every vcpkg dependency from source. Expect **30–60+ minutes** and several GB of disk; subsequent configures finish in seconds.

#### Platform notes

- **macOS** — `xcode-os` and `ninja-os` (no arch suffix) pick the host architecture. Use the explicit `-arm64` / `-x64` preset to cross-build (e.g. an arm64 bundle from an Intel Mac).
- **Linux with Clang** (faster builds): append `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD` to the configure command. The hidden `lld` and `mold` presets set `CMAKE_LINKER_TYPE`, and `ccache` and `sccache` set the compiler launcher, for a preset of your own in `CMakeUserPresets.json`, for example `{"name": "mine", "inherits": ["ninja-os", "mold", "ccache"]}`. A compiler cache needs `/Z7`-style debug info on MSVC, which this tree does not use, so the launcher presets are for Linux and macOS.
- **vcpkg triplet** — chosen from the generator, the architecture and `AL_ISA_TIER`: `<arch>-<os>-alchemy[-avx2|-avx512][-release]`, where `-release` means a single-configuration tree that is not Debug and skips the debug ports. Pass `-DVCPKG_TARGET_TRIPLET=<name>` to choose one yourself; CI does, to take release-only ports under a multi-config generator.

### Workflow presets (one-shot configure + build)

Workflow presets run configure and build as a single command. Useful for CI and one-off release builds:

```
cmake --workflow --preset ninja-os-release
cmake --workflow --preset vs2026-os-release
cmake --workflow --preset xcode-os-release
cmake --workflow --preset vs2026-os-fullopt-release
```

See `workflowPresets` in the generator files under `indra/cmake/presets/` for the full set.

## Build

After configuring, build with CMake or your IDE.

### From the command line

```
# Multi-config generators (VS, Xcode, Ninja Multi-Config)
cmake --build <build-dir> --config Release

# Or use a build preset
cmake --build --preset ninja-os-release
```

### From an IDE

```
# Visual Studio
start .\build-Windows-vs2026-os\Alchemy.slnx

# Xcode
open ./build-Darwin-xcode-os-arm64/Alchemy.xcodeproj
```

> `.slnx` is the newer Visual Studio solution format. Requires VS 2026.

### Output locations

The viewer executable lands under `build-<OS>-<preset>/newview/<Config>/`:

| Platform | Path                                                        |
|:---------|:------------------------------------------------------------|
| Windows  | `build-Windows-<preset>\newview\<Config>\<ChannelName>.exe` |
| macOS    | `build-Darwin-<preset>/newview/<Config>/<ChannelName>.app`  |
| Linux    | `build-Linux-<preset>/newview/<Config>/<ChannelName>`       |

`<ChannelName>` follows `AL_CHANNEL` (default `Alchemy Test` → `AlchemyTest.exe` / `AlchemyTest.app`).

## Configuration types

Ninja and Xcode presets are multi-config; Visual Studio presets always are. Every configure preset has a build preset per configuration, named `<preset>-<config>` in lower case: `ninja-os-debug`, `ninja-os-optdebug`, `ninja-os-relwithdebinfo`, `ninja-os-release`, and likewise for the others. `--config <Config>` on the command line overrides the preset's configuration.

| Configuration    | Libraries | Asserts | Notes                                                 |
|:-----------------|:----------|:--------|:------------------------------------------------------|
| `Debug`          | debug     | yes     | Slowest; full debugging of viewer and deps            |
| `OptDebug`       | release   | yes     | Optimized libs with debuggable viewer code            |
| `RelWithDebInfo` | release   | yes     | Default for Ninja presets; ship-adjacent with asserts |
| `Release`        | release   | no      | Ship builds                                           |

## Build options

Override any option at configure time with `-D<NAME>=<VALUE>`. For example:

```
cmake -S indra --preset ninja-os -DAL_BUILD_TESTS=ON -DAL_USE_FMODSTUDIO=ON
```

Options are defined in [`indra/CMakeLists.txt`](../indra/CMakeLists.txt). The most commonly used:

### Build targets

| Option                  | Default | Description                                                           |
|:------------------------|:--------|:----------------------------------------------------------------------|
| `AL_BUILD_VIEWER`          | ON      | Build the viewer executable                                           |
| `AL_BUILD_APPEARANCE_UTILITY` | OFF     | Build the appearance utility                                          |
| `AL_BUILD_TESTS`         | OFF     | Build and run unit + integration tests                                |
| `AL_BUILD_DOCS`          | OFF     | Add the `doc` target (API documentation with Doxygen)                 |
| `AL_VCPKG_INSTALL`       | ON      | Let configure run `vcpkg install` when the manifest, the registry configuration, the triplets or the feature list changed; off leaves the ports to you |
| `AL_BUILD_PACKAGE`       | ON      | Add the `package` target: the CPack archive of the installed tree (zip, tar.xz, dmg) |
| `AL_USE_VELOPACK`        | OFF     | Add the `velopack` target, and the Velopack update client to the viewer |
| `AL_SOURCEID`            | `$sourceid` | Referring agency recorded in `settings_install.xml`                |

### Audio

| Option              | Default | Description                                                          |
|:--------------------|:--------|:---------------------------------------------------------------------|
| `AL_USE_FAUDIO`     | ON      | FAudio audio engine                                                  |
| `AL_USE_OPENAL`     | OFF     | OpenAL audio engine                                                  |
| `AL_USE_FMODSTUDIO` | OFF     | FMOD Studio audio engine (proprietary; `AL_FMODSTUDIO_SDK_DIR` names the SDK, or the Windows installer's registry entry does) |

### Proprietary SDKs

| Option           | Default | Description                                                                 |
|:-----------------|:--------|:----------------------------------------------------------------------------|
| `AL_ENABLE_PROPRIETARY` | OFF | Allow the non-free libraries below                                     |
| `AL_USE_KDU`     | ON      | Kakadu JPEG2000 codec (needs `AL_ENABLE_PROPRIETARY`)                       |
| `AL_USE_DISCORD` | OFF     | Discord presence through the Social SDK (needs `AL_ENABLE_PROPRIETARY`; `AL_DISCORD_SDK_DIR` names the SDK unpacked from the developer portal) |

### Profiling

| Option                 | Default            | Description                               |
|:-----------------------|:-------------------|:------------------------------------------|
| `AL_USE_TRACY`            | ON for test builds | Tracy profiler support                    |
| `AL_ENABLE_TRACY_ON_DEMAND`  | ON                 | Only profile when a Tracy server connects |
| `AL_ENABLE_TRACY_LOCAL_ONLY` | ON                 | Disallow remote Tracy profiling           |
| `AL_ENABLE_TRACY_GPU`        | OFF                | Tracy GPU profiling                       |

### Optimization / instrumentation

| Option                                            | Default | Description                                                        |
|:--------------------------------------------------|:--------|:-------------------------------------------------------------------|
| `AL_USE_LTO`                     | OFF     | Link Time Optimization                                                      |
| `AL_ISA_TIER`                    | `v3`    | x86-64 level for the viewer and its vcpkg ports: `baseline`, `v2` (SSE4.2), `v3` (AVX2), `v4` (AVX-512). Ignored on macOS |
| `AL_SANITIZERS`                  | empty   | Any of `address`, `undefined`, `thread` (GCC and Clang only)                |
| `AL_ENABLE_WARNINGS_AS_ERRORS`   | ON      | Treat compiler warnings as errors                                           |
| `AL_ENABLE_RELEASE_DEBUG_LOGGING`| Test channel only | Keep debug-level logging in Release builds                        |
| `AL_USE_WEBRTC`                  | ON      | WebRTC voice (off automatically in sanitized builds)                        |

### Media plugins

| Option                   | Default     | Description                                |
|:-------------------------|:------------|:-------------------------------------------|
| `AL_BUILD_CEF_PLUGIN`       | ON          | Chromium Embedded Framework (in-world web) |
| `AL_BUILD_VLC_PLUGIN`       | ON          | VLC media plugin                           |
| `AL_BUILD_GSTREAMER_PLUGIN` | ON on Linux | GStreamer media plugin (Linux only)        |
| `AL_BUILD_EXAMPLE_PLUGIN`   | ON          | Reference/example plugin                   |

### Platform-specific

| Option           | Default     | Description                                            |
|:-----------------|:------------|:-------------------------------------------------------|
| `AL_USE_OPENXR`     | OFF         | OpenXR VR support (experimental)                       |
| `AL_USE_SDL_WINDOW` | ON on Linux | SDL-based window management (Linux only; Wayland path) |

### Crash reporting

| Option                        | Default | Description                                |
|:------------------------------|:--------|:-------------------------------------------|
| `AL_USE_SENTRY`                  | OFF     | Sentry crash reporting                     |
| `AL_ENABLE_CRASH_REPORTING`   | OFF     | Send crash reports from this build         |

Every option the project defines carries the `AL_` prefix. Booleans use one of
three verbs: `AL_BUILD_<x>` produces a target or artifact, `AL_USE_<x>` pulls
in a dependency or picks a backend, `AL_ENABLE_<x>` switches a behaviour.
Values are `AL_<NOUN>`. Configuring with a name from before this scheme
prints a warning naming the replacement.

See [`indra/CMakeLists.txt`](../indra/CMakeLists.txt) for the complete list.

## CMake style

The CMake files are formatted with [gersemi](https://github.com/BlankSpruce/gersemi) (`pip install gersemi`); the configuration is `.gersemirc` at the repository root, and it reads the project's own command definitions from `indra/cmake` so `al_add_test` and friends format like the built-ins. Format what you touched before committing:

```
gersemi -i indra/CMakeLists.txt indra/cmake/*.cmake indra/*/CMakeLists.txt
```

`gersemi --check` on the same paths reports what would change without changing it.

## Running tests

Enable tests at configure time:

```
cmake -S indra --preset <preset> -DAL_BUILD_TESTS=ON
```

Four tests drive a Python peer (`llleap`, `llprocess`, `llsdserialize`, `llcorehttp`); they need a Python 3 interpreter with the `llsd` package (`pip install -r requirements.txt`, in a venv if you like) and are registered disabled when configure finds none. Nothing else in the build runs Python.

Build, then run with CTest:

```
cmake --build <build-dir> --config RelWithDebInfo
ctest --test-dir <build-dir> --output-on-failure
```

Unit tests live alongside the library they cover in `indra/<library>/tests/`, written against the TUT (Template Unit Test) framework. Integration tests are in `indra/integration_tests/`.

## Editing XUI

`indra/newview/skins/xui.xsd` is the widget vocabulary: every registered tag, the attributes its parameter block answers to, the parameter elements it takes and the tags valid below it. Point an XML editor at it and a XUI file gets completion and a warning on a name no widget has.

The file is written out of the viewer's own registries, since the viewer is the only place all of them exist: run a developer build, open XUI Studio (Advanced &gt; XUI / Colors &gt; XUI Studio) and press **Schema**. `llui_libtest --schema` writes the same thing for the widgets `llui` registers, which is the part a test in that library can check.

VS Code, with the Red Hat XML extension:

```json
"xml.fileAssociations": [
  { "pattern": "**/skins/**/xui/**/*.xml", "systemId": "indra/newview/skins/xui.xsd" }
]
```

It is regenerated rather than edited, and it is permissive where XUI is ambiguous. A parameter may be written as an attribute or as a nested element, and a colour, image, font or setting name is a string whose vocabulary lives in another file. Those are for the tool's lint to check, not a schema.

A few files under `xui/` are data rather than widget trees — `strings.xml`, `mime_types.xml`, the `llsd` files, the `contents` tables. The schema has no root for those and an editor will say so on their first line; the association is by path and cannot tell them apart.

## Packaging

The install rules in `indra/cmake/ViewerInstall.cmake` are the package manifest. After every link of the viewer they stage the tree it runs from into the build directory (`newview/<Config>/`, or `newview/<Config>/<Channel>.app` on macOS). The same rules write a clean tree anywhere:

```
cmake --install build-<OS>-<preset> --config Release --prefix <dir>
```

The archive of that tree comes from CPack — a `.zip` on Windows, a `.tar.xz` on Linux, a `.dmg` on macOS — into the build directory, named `Alchemy[_<channel>]_<version>_<arch>`:

```
cpack --config build-<OS>-<preset>/CPackConfig.cmake -C Release
```

(or the `package` target). Release archives on Linux and macOS are stripped of debug information on the way. `-DAL_BUILD_PACKAGE=OFF` leaves CPack out; the install rules stay.

The Windows installer and the update packages come from [Velopack](https://velopack.io): configure with `-DAL_USE_VELOPACK=ON`, run `dotnet tool restore` once so the `vpk` tool is available, and build the `velopack` target. It installs into `newview/velopack/<Config>/app` and writes the installer and the update feed to `newview/velopack/<Config>/Releases`.

The third-party attribution is generated, not kept by hand: `cmake/Attribution.cmake` reads every installed port's `vcpkg.spdx.json` and `copyright` and writes `app_settings/packages-info.txt` (what the About floater's Licences tab shows) and `licenses.txt` (every licence text). What vcpkg cannot know — the pieces under `indra/externals/`, the SDKs from outside vcpkg, and a holder or licence a port's files do not state — is in `cmake/attribution.json`, as is the list of installed ports that ship nothing and are skipped: build tools, empty ports that stand for a system library, and what is built only for those. A newly added port whose `vcpkg.json` declares no `license` stops the build with its name; fix the port, add an override to the table, or, if the viewer ships none of it, skip it with the reason (and the platform, when the port is empty only on some).

On macOS the install step signs the bundle inside out — ad-hoc, or with `-DAL_ENABLE_SIGNING=ON -DAL_SIGNING_IDENTITY=<Developer ID>` — so the CEF helpers keep their sandbox entitlements. On Linux the binaries carry an `$ORIGIN`-relative RPATH and find the data one directory above the executable, so the tree runs from wherever it is unpacked.

## Troubleshooting

### Configure fails: `indra/dullahan` has no `CMakeLists.txt`

The Dullahan CEF wrapper is a git submodule. If you cloned without `--recurse-submodules`, `indra/dullahan` is empty and CMake configure stops with an error like:

```
CMake Error at CMakeLists.txt (add_subdirectory):
  The source directory .../indra/dullahan does not contain a CMakeLists.txt file.
```

Fetch the submodule, then re-run configure:

```
git submodule update --init --recursive
```

### First `cmake -S indra --preset ...` takes forever

Expected on the first run: vcpkg downloads and builds every C/C++ dependency from source. Budget **30–60+ minutes** and several GB of disk. Subsequent configures reuse the vcpkg cache and finish in seconds.

If the run produces no output for a very long time it usually isn't hung — check CPU and disk activity before killing it.

### CMake is too old

Alchemy requires CMake 4.0+. If your distro ships something older, install a newer version via pip:

```
pip install --upgrade cmake ninja
```

### `vpk` command not found (the `velopack` target fails)

Velopack needs the `vpk` .NET tool. Install it once per clone:

```
dotnet tool restore
```

### Rust / `cargo` missing during packaging

Velopack invokes `cargo`. Install a stable Rust toolchain:

```
rustup default stable
```

Only needed with `AL_USE_VELOPACK=ON`.

### Warnings fail the build

By default, warnings are treated as errors. New compiler releases sometimes introduce diagnostics the tree hasn't yet cleaned up. Disable fatal warnings at configure time:

```
cmake -S indra --preset vs2026-os -DAL_ENABLE_WARNINGS_AS_ERRORS=OFF
```

### Visual Studio doesn't recognize `Alchemy.slnx`

`.slnx` is the newer Visual Studio solution format. Use Visual Studio 2022 17.10+ or Visual Studio 2026, or configure with the `vs2022-os` preset on an older compatible edition.

### `cmake --build --preset ninja-os-release` fails with "no such preset"

You probably configured with a proprietary preset (e.g. `ninja`, without the `-os` suffix). Build presets are tied to configure presets — use the matching build preset for whichever configure preset you used (for example `ninja-release` for `ninja`).

### Linux: missing system headers during vcpkg builds

Double-check the package list for your distro under [Platform setup → Linux](#linux). Common offenders when a package lookup produces an error like `<something>.h not found`:

- `autoconf-archive` — required by several vcpkg ports
- `libxkbcommon-dev`, `libwayland-dev`, `wayland-protocols` — required for SDL window and Wayland support
- `libgstreamer-plugins-base1.0-dev` — required for the GStreamer media plugin

### Still stuck?

- Ask on the [Discord](https://discordapp.com/invite/KugCgs6).
- File a build bug at <https://github.com/AlchemyViewer/Alchemy/issues>.

## See also

- [Contributing](../CONTRIBUTING.md)
- [Architecture](ARCHITECTURE.md)
