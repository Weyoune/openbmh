# Third-party notices

OpenBMH's own code and documentation use GPL-3.0-only. This does not replace the licenses of the components below. Their upstream copyright and permission notices remain in the source and in [docs/licenses](docs/licenses/README.md). The original game's executable, data, graphics, text and audio are supplied separately by the player and are not licensed or distributed by this project.

| Component | Version used by the build | License | Included notice |
|---|---|---|---|
| [SDL2](https://github.com/libsdl-org/SDL/tree/release-2.32.10) | 2.32.10, or an explicitly selected system SDL2 | zlib license | [SDL2.txt](docs/licenses/SDL2.txt) |
| SDL2 HIDAPI code | Included with SDL2 | BSD-3-Clause option; upstream also offers GPL-3.0 and its original license | [license options](docs/licenses/SDL2-HIDAPI-options.txt), [BSD text](docs/licenses/SDL2-HIDAPI-BSD.txt), [original text](docs/licenses/SDL2-HIDAPI-original.txt) |
| SDL2 yuv2rgb code | Included with SDL2 | BSD-3-Clause | [SDL2-yuv2rgb.txt](docs/licenses/SDL2-yuv2rgb.txt) |
| [miniz](https://github.com/richgel999/miniz/releases/tag/3.0.2) | 3.0.2 amalgamation | MIT; embedded public-domain notices remain in its source | [miniz.txt](docs/licenses/miniz.txt), [source notices](docs/licenses/miniz-source-notices.txt) |
| [LZMA SDK decoder files](https://github.com/ip7z/7zip/tree/24.09/C) | Selected C files from 7-Zip 24.09 | Public domain, as marked in the selected files | [LZMA-SDK.txt](docs/licenses/LZMA-SDK.txt) |
| [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended/tree/v1.2.1) | 1.2.1 | zlib license | [nativefiledialog-extended.txt](docs/licenses/nativefiledialog-extended.txt) |
| [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3/tree/765ec962e473aeb767e4cba74ffdc8f588ffbfe8) | Commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8` | LGPL-2.1-or-later | [LGPL-2.1.txt](docs/licenses/LGPL-2.1.txt), [copyright and source notice](docs/licenses/Nuked-OPL3-notices.txt) |

The miniz implementation provides a zlib-compatible API; it is not the zlib library. OpenBMH does not directly link a separate zlib dependency. SDL2 and nativefiledialog-extended use the **zlib license**, which is distinct from using zlib code. A system-library build or distributor's package can have additional runtime dependencies; its notices must match the libraries actually included.

Only the public-domain C decoding subset is taken from the 7-Zip source package. This statement does not describe all of 7-Zip: other parts of that upstream project have different licenses and are not included here.

Nuked OPL3 is linked into the audio implementation. A binary distribution must retain its LGPL notices and provide the corresponding library source and the materials needed to exercise the LGPL's modification and relinking rights. For a source release, the vendored implementation and its build files are included. Binary distributors must also satisfy the source-distribution obligations of OpenBMH's GPL license. Merely copying license texts does not replace these obligations.

Native platform libraries, such as GTK on Linux, may be required at runtime even when they are not bundled. Compiler runtime files included with a binary package retain their own terms. See the package's file list and platform notes for the actual distribution.

## Natural Earth country outlines

The tiny England/Germany outlines in `src/ui/country_glyphs.hpp` are derived from Natural Earth's public-domain 1:50m admin-0 map subunits. They are geographic data, independent of original BMH/FLTD assets. Rendering and generator code are OpenBMH-owned GPL-3.0-only code.

- Source: https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_50m_admin_0_map_subunits.geojson
- Terms: https://www.naturalearthdata.com/about/terms-of-use/
- Input SHA-256: `b1df8433477614c89e6b724990a7ab3733ec647559863321b6255c3fa3fce3ec`
- Reproduce: `python3 tools/world12/generate_country_glyphs.py source.geojson src/ui/country_glyphs.hpp`

Made with Natural Earth. The original full geographic dataset is not needed at runtime or bundled with the game.
