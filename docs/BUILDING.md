# Building OpenBMH

Requirements: a C++20 compiler, CMake 3.24 or newer, Git and a supported build tool such as Ninja. The CMake project fetches pinned SDL2, miniz, 7-Zip C decoder, nativefiledialog-extended, RmlUi and FreeType sources. Network access is needed on the first configure unless these dependencies are supplied locally. Nuked OPL3 is included in the source tree.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The public checkout does not include game data. Tests that require a game installation are conditional; configure with `-DBMH_DATA="path/to/game"` to make locally available data eligible. Private saved-game fixtures are not supplied by the project. Read the test output to distinguish executed and skipped checks.

Main programs are `OpenBMH` (native launcher and graphical game) and `openbmh_cli` (console and protocol). Single-configuration generators put them in the build directory; multi-configuration generators normally add a configuration directory. On Windows they have the `.exe` suffix.

## Windows

Use an MSVC environment with C++20 support and its Windows SDK, or another toolchain explicitly supported by the project's build configuration. From a Developer PowerShell with CMake and Ninja available:

```powershell
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
ctest --test-dir build-windows --output-on-failure
```

With a Visual Studio generator, choose one actually provided by your installed CMake and add `--config Release` / `-C Release` to build and test commands. Native file dialogs and runtime-library deployment require the platform SDK and package-specific checks.

## Linux

Install a C++20 compiler, Git, CMake, Ninja, pkg-config and the development packages required by SDL2 and the folder-dialog backend. The RmlUI launcher uses SDL2/OpenGL; its native folder picker uses GTK3 on Linux. For example, Debian-family development packages include `build-essential`, `cmake`, `ninja-build`, `git`, `pkg-config`, `libgl1-mesa-dev` and `libgtk-3-dev`; SDL2's selected video/audio backends may need additional development libraries. RmlUI and FreeType are built from pinned sources without installing them separately. Keep the generated `assets/launcher` directory alongside OpenBMH when copying a development build.

The common commands above build the bundled SDL2. To use an installed SDL2 CMake package instead, configure with `-DBMH_USE_SYSTEM_SDL2=ON`. Its version and backend configuration then come from the installed package.

## macOS

Apple's **Command Line Tools are sufficient** for the compiler and SDK; the full Xcode application is not a prerequisite for a command-line build. Install the tools with `xcode-select --install`, and install CMake and Ninja separately. On an Apple Silicon machine:

```sh
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-macos
ctest --test-dir build-macos --output-on-failure
```

Use native toolchain/dependency architectures consistently. A terminal executable and a Finder-launchable, signed `.app` package are different deliverables. The commands describe the build procedure, not a claim that a particular Windows or macOS binary has passed native acceptance.

## Import and run your data

Start the native launcher, choose **Add**, and select your original installation. Content and personal files are portable beside the executable. The original installation is needed only for import. See the [0.2 contract](formats/obw/runtime-0.2.md).

```sh
build/OpenBMH
build/openbmh_cli --console --mod "build/mods/BMH-classic/content.obw" --seed 1
```

For redistribution, include [LICENSE](../LICENSE), [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md), the full texts in [licenses/](licenses/README.md), and the corresponding source and build information required by the applicable licenses. Inspect the actual libraries in each platform package; static SDL2 alone does not establish that every dependency is static.
