<picture>
  <source srcset="doc/alchemy_logo.png">
  <img alt="Alchemy Viewer Logo" src="doc/alchemy_logo.png">
</picture>

# Alchemy Viewer

[Alchemy Viewer](https://www.alchemyviewer.org) is a third-party client for Second Life. Our focus is on creating a cohesive and modern experience, with carefully considered default behaviors and settings while maintaining a bleeding-edge approach to adopting new features and developments from the Second Life platform.

## Download

Most people use a pre-built release. Windows, macOS, and Linux builds are published as [releases on GitHub][releasesgh]. Release candidates and project viewers are typically announced on our [Discord server][discord].

## Building from source

Alchemy uses CMake with vcpkg for dependency management.

### Quick start

```
git clone https://github.com/alchemyviewer/alchemy.git
cd alchemy
python3 -m venv .venv && source .venv/bin/activate   # Windows: .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
dotnet tool restore                                   # only if packaging
cmake -S indra --preset <preset>                      # see below
cmake --build build-<OS>-<preset> --config Release
```

### Platform setup

Install the right compiler and system dependencies for your OS first:

- [Windows](doc/BUILD.WINDOWS.md)
- [Linux](doc/BUILD.LINUX.md)
- [macOS](doc/BUILD.MAC.md)

### Reference

Presets, configuration types, build options, and how to run tests all live in [**doc/BUILD.md**](doc/BUILD.md).

## Contribute

Help improve Alchemy Viewer! You can get involved by filing bugs, suggesting enhancements, submitting pull requests, and more. See [CONTRIBUTING](CONTRIBUTING.md) for details.

## Resources

* [Website](http://www.alchemyviewer.org)
* [Downloads][releasesgh]
* [Issue tracker](https://github.com/AlchemyViewer/Alchemy/issues)
* [Discord][discord]

[releasesgh]: https://github.com/AlchemyViewer/Alchemy/releases
[discord]: https://discordapp.com/invite/KugCgs6
