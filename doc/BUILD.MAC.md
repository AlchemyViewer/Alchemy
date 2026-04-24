# Building on macOS

For presets, options, configuration types, and tests, see [BUILD.md](BUILD.md). This page only covers macOS-specific setup.

## 1. Install tools

- [Xcode](https://developer.apple.com/xcode/) — install from the App Store, then run `xcode-select --install`
- [Homebrew](https://brew.sh/)

Then install build dependencies via Homebrew:

```
brew install git cmake zip unzip curl pkgconf automake autoconf autoconf-archive \
    gettext libtool rustup dotnet
```

Initialise the Rust toolchain (only needed if you will package):

```
rustup-init -y
```

## 2. Clone and bootstrap

```
git clone https://github.com/alchemyviewer/alchemy.git
cd alchemy
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
dotnet tool restore        # only if you plan to produce installer packages
```

## 3. Configure

Pick a preset based on your CPU and preferred generator. Apple Silicon (M-series) uses arm64; Intel Macs use x86_64.

| Generator | Apple Silicon            | Intel                   |
|:----------|:-------------------------|:------------------------|
| Xcode     | `xcode-os-arm64`         | `xcode-os-x64`          |
| Ninja     | `ninja-os-arm64`         | `ninja-os-x64`          |

`xcode-os` (no arch suffix) also works and picks your host architecture.

```
cmake -S indra --preset xcode-os-arm64
```

## 4. Build

### From Xcode

```
open ./build-Darwin-xcode-os-arm64/Alchemy.xcodeproj
```

### From Terminal

```
cmake --build build-Darwin-xcode-os-arm64 --config Release
```

Ninja users:

```
cmake --build --preset ninja-os-arm64-release
```

The viewer app lands at:

```
build-Darwin-<preset>/newview/<Config>/<ChannelName>.app
```

where `<ChannelName>` follows `VIEWER_CHANNEL` (default `Alchemy Test` → `AlchemyTest.app`).

---

Common problems are covered in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
