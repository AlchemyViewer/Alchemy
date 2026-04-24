# Building on Windows

For presets, options, configuration types, and tests, see [BUILD.md](BUILD.md). This page only covers Windows-specific setup.

## 1. Install tools

- [Visual Studio 2026](https://visualstudio.microsoft.com/vs/community/) or Visual Studio 2022 — select the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 3.27+
- [Git for Windows](https://git-scm.com/install/windows)
- [Python 3.13+](https://www.python.org/downloads/) — tick **Add Python to PATH** during install
- [Rust](https://rust-lang.org/tools/install/) — run `rustup-init.exe`, accept defaults (only needed if you'll package; see [BUILD.md](BUILD.md#prerequisites))
- [.NET SDK](https://dotnet.microsoft.com/en-us/download) (same caveat)

Sanity check in a fresh terminal:

```
cmake --version
python --version
git --version
```

## 2. Clone and bootstrap

Open PowerShell:

```
git clone https://github.com/alchemyviewer/alchemy.git
cd alchemy
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
dotnet tool restore        # only if you plan to produce installer packages
```

## 3. Configure

```
cmake -S indra --preset vs2026-os
```

For VS 2022 use `vs2022-os` instead. The first configure run downloads and builds every vcpkg dependency from source; expect it to take 30–60+ minutes and several GB of disk on the first run. Subsequent configures are fast.

## 4. Build

### From Visual Studio

```
start .\build-Windows-vs2026-os\Alchemy.slnx
```

Then build from the IDE as usual. Note: `.slnx` is the newer solution format — if your Visual Studio is older and doesn't recognise it, upgrade VS or use a current edition.

### From the command line

```
cmake --build build-Windows-vs2026-os --config Release
```

The viewer lands at:

```
build-Windows-vs2026-os\newview\<Config>\<ChannelName>.exe
```

where `<ChannelName>` follows `VIEWER_CHANNEL` (default `Alchemy Test` → `AlchemyTest.exe`).

---

Common problems are covered in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
