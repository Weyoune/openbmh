# OpenBMH

The separate [OpenBMH Editor 0.5.5](docs/EDITOR.md) uses native RmlUI,
supports German/English, three themes, image editing and a local MCP server.
Download its own `editor-v0.5.5` release or build with
`-DOPENBMH_BUILD_EDITOR=ON`. Edited worlds are independent of the Classic runtime.

A modern and faithful open source rebuild of the *Bundesliga Manager Hattrick* engine.

OpenBMH is a standalone C++20 engine with an SDL2 interface for *Bundesliga Manager Hattrick* (1994/95) and its English edition, *Football Limited*. The project develops a playable reconstruction alongside a platform for new rules, content and interfaces. Keeping the familiar game enjoyable and making it easier to extend are equally important goals.

Version 0.2 imports a game installation supplied by the player into a portable Classic mod. The running game reads the converted OBW content. Game data, artwork, music, manuals and the original executable are not distributed with OpenBMH. Its aim is recognisable behaviour and plausible results; documented compatibility decisions and safety fixes take precedence over reproducing every original fault.

This is an independent community project, with no affiliation with or endorsement by the original rights holders, the DFL or the Bundesliga. It is an engine implementation, not an official source release or a remaster.

## Get started

Check the [releases page](https://github.com/Weyoune/openbmh/releases) for available packages and their platform notes. A Windows package uses the name `OpenBMH-<version>-windows-x64.zip`. Availability and native validation are stated for each release; source build instructions do not establish platform support on their own.

These instructions describe **0.2.1**, released separately from the editor. See the [0.2 release notes](docs/release/0.2/RELEASE_NOTES.md).

1. Extract OpenBMH into a writable directory.
2. Start `OpenBMH.exe` on Windows, or `OpenBMH` on Linux/macOS. Its RmlUI launcher initially has an empty **Name | Art** list. The night-stadium design uses a small version label in the lower-right corner.
3. Choose **Add**, select your own BMH or Football Limited installation, and enter a unique mod name and starting display year. The default names are `BMH-classic` and `FTDL-classic` (the English format identifier is `FLTD-classic`).
4. Optionally transfer saved games and settings from the installation or another folder. The import reports files it could not migrate.
5. Select the resulting Classic mod and choose **Start**. After import, its original installation is no longer needed for play.

Converted contents live in `mods/<name>/content.obw` next to the program. Saves (`.obs`), settings (`.obsettings`), desktop layouts (`.oblayout`) and high scores (`.obscore`) live separately in `userdata/<content-id>/`. **Delete** asks before removing a mod and retains personal data; importing identical contents reconnects them. **Rename** renames the mod directory. Externally modified Classic packages are rejected; import the installation again to repair them. Keep backups of the portable directory.

For direct startup or the console:

```sh
./OpenBMH --mod "mods/BMH-classic/content.obw"
./openbmh_cli --console --mod "mods/BMH-classic/content.obw"
```

Both frontends use native content and saves. Use **Add** to migrate older files rather than passing an original executable or `.FBM` to the running game. The starting year changes the Classic year display; original weekday and scheduling rules are preserved. Modern worlds are reserved for a later version.

The launcher uses SDL2 and RmlUI, with Direct3D 11 and automatic WARP software fallback on Windows, OpenGL 3.3 on Linux and OpenGL 4.1 on macOS; keep its `assets` directory next to the executable. Folder selection uses the operating system's dialog. See the [0.2 migration contract](docs/formats/obw/runtime-0.2.md) for compatibility and validation limits. Platform builds and automated tests are separate from human interactive acceptance on Windows and macOS.

## Features and direction

The graphical client and console share career management, matches, transfers, contracts, finances, training, stadium management and save handling. The console supports command batches, structured views and multiple manager clients. Audio is decoded from the user's installation where supported.

Version 0.1 establishes the playable engine and development interfaces. Later milestones introduce an open data format and typed model, configurable rules, a scalable interface and independently distributable content. These are goals rather than delivery dates; the [roadmap](docs/ROADMAP.md) describes their dependencies.

The standalone [`openbmh_import` tool](docs/formats/obw/import.md) produces the same complete Classic content packages, including media and frozen player portrait recipes. A mod directory contains its sealed `content.obw`; legacy save migration is performed through the launcher.

## Build from source

The following recipes target version 0.2. They require Git, CMake 3.24 or newer and a C++20 compiler. The first configure fetches pinned open-source dependencies. Native build and package results are documented for each release; these instructions do not claim an untested platform result.

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
.\build-windows\Release\OpenBMH.exe
```

### Linux

On Debian or Ubuntu, install the compiler and development libraries. This example uses the default GTK3 file dialog backend:

```sh
sudo apt install build-essential git cmake ninja-build pkg-config libgtk-3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libasound2-dev libpulse-dev libgl1-mesa-dev libudev-dev
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
./build-linux/OpenBMH
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
./build-macos/OpenBMH
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

Release source is identified by the matching version tag: [v0.1.2](https://github.com/Weyoune/openbmh/tree/v0.1.2) for the published 0.1.2 version. This branch develops 0.2; it is not a published release. Binary releases must accompany or offer the corresponding source and build/relinking materials required by the applicable licenses. The tag identifies application source; it does not replace required materials for linked third-party components.

Bundled components retain their own licenses, including Nuked OPL3 under LGPL-2.1-or-later. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).
