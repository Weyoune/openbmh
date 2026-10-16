# Platform notes

[BUILDING.md](BUILDING.md) contains Windows, Linux and macOS build commands. C++20, CMake and platform SDKs are required. macOS command-line builds need Apple's Command Line Tools, not the complete Xcode application.

A successful cross-build, source review or Linux run does not establish a native Windows/macOS result. Release notes must state which packaged platforms were actually validated. Packaging is separate from compilation: check runtime dependencies, writable save directories, file dialogs, Unicode paths, display scaling and audio on the target platform.

On macOS an unsigned terminal executable does not automatically provide Finder integration, an application bundle or notarisation. On Windows a statically built SDL2 does not remove the need to check compiler runtimes. System-library builds inherit the versions and backend choices of their installed dependencies.

All packages require the user's game data. No platform package may include original executables or extracted resources. See [third-party notices](../THIRD_PARTY_NOTICES.md) for distribution material.
