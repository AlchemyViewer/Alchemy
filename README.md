<picture>
  <source srcset="doc/alchemy_logo.png">
  <img alt="Alchemy Viewer Logo" src="doc/alchemy_logo.png">
</picture>

# Alchemy Viewer

[![License: LGPL 2.1](https://img.shields.io/badge/License-LGPL_2.1-blue.svg)](LICENSE)
[![Latest Release](https://img.shields.io/github/v/release/AlchemyViewer/Alchemy)](https://github.com/AlchemyViewer/Alchemy/releases)
[![Discord](https://img.shields.io/badge/Discord-Join-7289da?logo=discord&logoColor=white)][discord]

[Alchemy Viewer](https://www.alchemyviewer.org) is a third-party client for [Second Life](https://secondlife.com), forked from the official [Linden Lab viewer](https://github.com/secondlife/viewer). We focus on a cohesive, modern experience built on thoughtful defaults — while staying on the bleeding edge of new platform features.

## 📥 Download

Most users install a [pre-built release][releasesgh] for Windows, macOS, or Linux. Release candidates and project viewers are announced on our [Discord server][discord].

## 🔨 Building from source

Alchemy uses CMake with vcpkg for dependency management. Platform setup, presets, build options, tests, packaging, and troubleshooting all live in [**doc/BUILD.md**](doc/BUILD.md).

```
git clone --recurse-submodules https://github.com/AlchemyViewer/Alchemy.git alchemy
cd alchemy
python3 -m venv .venv && source .venv/bin/activate   # Windows: .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
dotnet tool restore                                  # packaging only
cmake -S indra --preset <preset>                     # see BUILD.md for presets
cmake --build build-<OS>-<preset> --config Release
```

## 🤝 Contribute

File bug reports, suggest enhancements, or open a pull request — see [CONTRIBUTING](CONTRIBUTING.md) for guidelines.

## 🙏 Acknowledgements

Alchemy stands on the work of:

- [Linden Lab](https://www.lindenlab.com/) and the [Second Life Viewer](https://github.com/secondlife/viewer) contributors
- The many open-source libraries that power the viewer (see [`indra/vcpkg.json`](indra/vcpkg.json))

## 🔗 Resources

- [Website](https://www.alchemyviewer.org)
- [Downloads][releasesgh]
- [Issue tracker](https://github.com/AlchemyViewer/Alchemy/issues)
- [Discord][discord]

## 📜 License

Alchemy is licensed under the [GNU Lesser General Public License v2.1](LICENSE), inherited from the upstream Linden Lab viewer.

[releasesgh]: https://github.com/AlchemyViewer/Alchemy/releases
[discord]: https://discordapp.com/invite/KugCgs6
