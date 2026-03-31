<picture>
  <source srcset="doc/alchemy_logo.png">
  <img alt="Alchemy Viewer Logo" src="doc/alchemy_logo.png">
</picture>

[Alchemy Viewer](https://www.alchemyviewer.org) is a third-party client for Second Life. Our focus is on creating a cohesive and modern experience, with carefully considered default behaviors and settings while maintaining a bleeding-edge approach to adopting new features and developments from the Second Life platform.

## Download

Most people use a pre-built release of Alchemy Viewer. Windows macOS, and Linux builds are published as [releases on Github][releasesgh]. More experimental releases, such as release candidates and project viewers, are typically announced on our [Discord server][discord].

## Build Instructions

Alchemy Viewer uses CMake for build system generation and vcpkg for dependency management. 

### Platform-specific setup guides

[Windows](doc/BUILD.WINDOWS.md)

[Mac](doc/BUILD.MAC.md)

[Linux](doc/BUILD.LINUX.md)

### Configuration Types
| CMake                      | Description                                                                         |
|:---------------------------|:------------------------------------------------------------------------------------|
| Debug                      | A debug build linked against debug libraries                                        |
| OptDebug                   | A debug build linked with release libraries                                         |
| RelWithDebInfo             | A release optimized build with asserts linked with release libraries                |
| Release                    | A release optimized build linked with release libraries                             |

### Build Options

| CMake                      | Description                                                                         | Default |
|:---------------------------|:------------------------------------------------------------------------------------|---------|
| BUILD_VIEWER               | Build viewer binaries                                                               | ON      |
| BUILD_APPEARANCE_UTIL      | Build appearance utility                                                            | OFF     |
| BUILD_TESTING              | Build test binries.                                                                 | OFF     |
| PACKAGE                    | Build installer packages when viewer build enabled                                  | ON      |
| USE_OPENAL                 | Build with support for the OpenAL audio engine                                      | ON      |

## Contribute

Help improve Alchemy Viewer! You can get involved with improvements by filing bugs, suggesting enhancements, submitting pull requests and more. See [CONTRIBUTING][] for details.

## Resources

* [Alchemy Website](http://www.alchemyviewer.org)
* [Downloads](https://alchemyviewer.org/downloads)
* [Issue Tracker](https://github.com/AlchemyViewer/Alchemy/issues)

[contributing]: https://github.com/alchemyviewer/alchemy/blob/main/CONTRIBUTING.md
[releasesgh]: https://github.com/AlchemyViewer/Alchemy/releases
[discord]: https://discordapp.com/invite/KugCgs6