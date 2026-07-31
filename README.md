<picture>
  <source srcset="doc/vayu_logo.png">
  <img alt="Vayu Viewer Logo" src="doc/vayu_logo.png">
</picture>

# Vayu Viewer

[![License: LGPL 2.1](https://img.shields.io/badge/License-LGPL_2.1-blue.svg)](LICENSE)
[![Latest Release](https://img.shields.io/github/v/release/Shadowolf7/Vayu-Viewer)](https://github.com/Shadowolf7/Vayu-Viewer/releases)

Vayu Viewer is a third-party client for [Second Life](https://secondlife.com), forked from [Alchemy Viewer](https://www.alchemyviewer.org), which is itself forked from the official [Linden Lab viewer](https://github.com/secondlife/viewer).

## 📥 Download

Pre-built releases for Windows, macOS, or Linux are published on the [releases page][releasesgh] once available.

## 🔨 Building from source

Vayu Viewer uses CMake with vcpkg for dependency management. Platform setup, presets, build options, tests, packaging, and troubleshooting all live in [**doc/BUILD.md**](doc/BUILD.md).

```
git clone --recurse-submodules https://github.com/Shadowolf7/Vayu-Viewer.git vayu-viewer
cd vayu-viewer
python3 -m venv .venv && source .venv/bin/activate   # Windows: .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
dotnet tool restore                                  # packaging only
cmake -S indra --preset <preset>                     # see BUILD.md for presets
cmake --build build-<OS>-<preset> --config Release
```

## 🤝 Contribute

File bug reports, suggest enhancements, or open a pull request — see [CONTRIBUTING](CONTRIBUTING.md) for guidelines.

## 🙏 Acknowledgements

Vayu Viewer stands on the work of:

- [Alchemy Viewer](https://www.alchemyviewer.org), the project this is forked from
- [Linden Lab](https://www.lindenlab.com/) and the [Second Life Viewer](https://github.com/secondlife/viewer) contributors
- The many open-source libraries that power the viewer (see [`indra/vcpkg.json`](indra/vcpkg.json))

## 🔗 Resources

- [Downloads][releasesgh]
- [Issue tracker](https://github.com/Shadowolf7/Vayu-Viewer/issues)

## 📜 License

Vayu Viewer is licensed under the [GNU Lesser General Public License v2.1](LICENSE), inherited from the upstream Alchemy Viewer and Linden Lab viewer.

[releasesgh]: https://github.com/Shadowolf7/Vayu-Viewer/releases
