# OpenBMH

A faithful, standalone community reconstruction of *Bundesliga Manager Hattrick*, using the player's original installation and SDL2 interface.

OpenBMH 1.0 is the Classic line, based on v0.1.2 with reviewed bug fixes. It starts directly with original data: no mod launcher, converter or OBW package is required. Original game files, graphics, audio and manuals are not distributed. German CD-compatible BMH and verified Diamond editions are the primary release targets. Football Limited remains experimental; the older floppy executable is not supported.

The former 0.3 / NextGen development continues separately as [Dreierpack Manager](https://github.com/Weyoune/Dreierpack-Manager). OpenBMH remains available for community maintenance and extensions of the original game.

This is an independent community project, with no affiliation with or endorsement by the original rights holders, the DFL or the Bundesliga. It is an engine implementation, not an official source release or a remaster.

## Get started

Check the [releases page](https://github.com/Weyoune/openbmh/releases) for available packages and their platform notes. A Windows package uses the name `OpenBMH-<version>-windows-x64.zip`. Availability and native validation are stated for each release; source build instructions do not establish platform support on their own.

1. Have your own supported game installation available.
2. Extract the OpenBMH package into a separate directory, or alongside the game files.
3. Start `OpenBMH` (`OpenBMH.exe` on Windows). Select the game directory or executable when prompted. A ZIP or 7z containing one game installation can also be selected.

From a terminal:

```sh
OpenBMH --data "path/to/game" --save-directory "path/to/saves"
openbmh_cli --console --data "path/to/game" --saves "path/to/saves"
```

Use a writable save directory and keep backups of existing games. German editions are the primary compatibility target. **Football Limited / English data support is experimental**; original V2.20 save import and some edition-specific behaviour remain limited. See the [0.1 release notes](docs/release/0.1/RELEASE_NOTES.md).

## Features and direction

The graphical client and console share career management, matches, transfers, contracts, finances, training, stadium management and save handling. The console supports command batches, structured views and multiple manager clients. Audio is decoded from the user's installation where supported.

OpenBMH focuses on Classic compatibility, stability and community contributions. Next-generation interfaces, open-world formats and configurable competition structures belong to Dreierpack Manager.

## Build from source

The following recipes target OpenBMH 1.0 release candidates. They require Git, CMake 3.24 or newer and a C++20 compiler. The first configure fetches pinned open-source dependencies. Native build and package results are documented for each release; these instructions do not claim an untested platform result.

Clone the public repository, then run the commands for your system:

```sh
git clone https://github.com/Weyoune/openbmh.git
cd openbmh
```

### Windows

Install Visual Studio 2022 or its Build Tools with **Desktop development with C++**, a Windows SDK and CMake. From Developer PowerShell, using the Visual Studio generator:

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
ctest --test-dir build-windows -C Release --output-on-failure
.\build-windows\Release\OpenBMH.exe --data "path/to/game" --save-directory "path/to/saves"
```

### Linux

On Debian or Ubuntu, install the compiler and development libraries. This example uses the default GTK3 file dialog backend:

```sh
sudo apt install build-essential git cmake ninja-build pkg-config libgtk-3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libasound2-dev libpulse-dev libgl1-mesa-dev libudev-dev
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
./build-linux/OpenBMH --data "path/to/game" --save-directory "path/to/saves"
```

Use a newer CMake if your distribution ships a version older than 3.24. A Linux binary can still require system libraries such as GTK3; a ZIP is not a guarantee of portability across distributions.

### macOS

Apple's Command Line Tools supply the compiler and SDK; full Xcode is not required. Install CMake and Ninja separately, for example with Homebrew. On Apple Silicon:

```sh
xcode-select --install
brew install cmake ninja
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-macos
ctest --test-dir build-macos --output-on-failure
./build-macos/OpenBMH --data "path/to/game" --save-directory "path/to/saves"
```

On Intel Macs omit the arm64 setting or select `x86_64`. These commands build terminal executables; they do not create a signed application bundle.

Game-data tests are optional: add `-DBMH_DATA="path/to/game"` when configuring to supply your own installation. The repository includes no original test data. See [BUILDING](docs/BUILDING.md) for test skips, system SDL2 and packaging details.

## Documentation

- [Build from source](docs/BUILDING.md) and [platform notes](docs/BUILD_PLATFORMS.md)
- [Architecture](docs/architecture.md), [file formats](docs/formats/README.md) and [random generators](docs/RNG.md)
- [Console reference](docs/CLI_CONCEPT/README.md), [structured protocol](docs/protocol.md) and [client/agent introduction](docs/CONSOLE_CLIENTS.md)
- [Compatibility decisions](docs/COMPATIBILITY.md)

## Contribute

Reports with reproducible steps, documentation corrections and focused patches are welcome. Discuss larger design changes in an issue before implementing them. [CONTRIBUTING.md](CONTRIBUTING.md) explains the review and testing conventions. Do not upload original game files, screenshots, recordings, copied text databases or saved games to public issues or pull requests.

## How the project was developed

The implementation grew from study of file formats and behaviour of a legally obtained game installation. The public repository contains independently written implementation and format documentation. It does not contain the proprietary implementation or its extracted resources.

## License and thanks

OpenBMH's own code and documentation are licensed under **GNU GPL version 3 only**, SPDX **GPL-3.0-only**. [LICENSE](LICENSE) contains the unchanged GPLv3 text; the example wording in its appendix does not change this project's version choice. Game content has separate ownership and is not covered by this license.

Release source is identified by the matching version tag: [v0.1.2](https://github.com/Weyoune/openbmh/tree/v0.1.2) for this version. Binary releases must accompany or offer the corresponding source and build/relinking materials required by the applicable licenses. The tag identifies application source; it does not replace required materials for linked third-party components.

Bundled components retain their own licenses, including Nuked OPL3 under LGPL-2.1-or-later. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).
