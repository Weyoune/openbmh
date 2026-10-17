# File formats

OpenBMH reads files from a game installation supplied by the user. This directory documents their structure and the implementation's interfaces; it does not distribute game content or inventories of extracted text or artwork.

Unless a page says otherwise, integer fields are little endian. Legacy strings use a single-byte encoding; the German character substitutions are handled by `iso646de_to_utf8` in `src/formats/binary.cpp`. Text presented to clients is converted to UTF-8. Do not interpret a legacy byte offset as a Unicode character index.

| Data | Files | Documentation | Implementation |
|---|---|---|---|
| Palette | `M3PC.COL` | [COL](COL.md) | `src/formats/palette.cpp` |
| Indexed images | `KB/*.VGA`, `W/*.VGA` | [VGA](VGA.md) | `src/formats/vga.cpp` |
| Bitmap fonts | `F/*.FNT` | [FNT](FNT.md) | `src/formats/font.cpp` |
| Text database | `TEXTD.BLK`, `TEXTE.BLK` | [TEXTD](TEXTD.md) | `src/formats/textd.cpp` |
| Master database | `M3D.MAN`, `M3E.MAN` | [M3D](M3D.md) | `src/formats/m3d.cpp` |
| Desktop layout | `M3.MEN`, `*.MEN` | [M3MEN](M3MEN.md) | `src/ui/desk_state.cpp` |
| Formation coordinates | `M3.DAT` | [M3DAT](M3DAT.md) | `src/game/state.cpp` |
| Settings | `*.EIN`, saved settings block | [EIN](EIN.md) | `src/game/state.hpp` |
| Save container and state | `*.FBM` | [FBM](FBM.md), [STATE](STATE.md) | `src/formats/fbm.cpp`, `savestate.cpp` |
| Legacy configuration | `SOUND.CFG`, `PFAD.MAN` | [CONFIG](CONFIG.md) | Not used for host device configuration |
| Audio containers | `S/2000` through `S/2003` | [SOUND](SOUND.md) | `src/audio/container.cpp`, `original.cpp` |
| Executable data | German LE and English X-32 images | [LE](LE.md), [executable layout](EXECUTABLE_LAYOUT.md) | `src/formats/le.cpp`, `src/launch/fltd.cpp` |
| IFF crest template | `WAPPEN.LBM` | [WAPPEN](WAPPEN.md) | Not loaded by OpenBMH |
| OpenBMH world, save and pack | `*.obw`, `*.obs`, `*.obp`, `*.obt` | [OBW](OBW.md), [initial importer](obw/import.md) | `src/import` converts installation data; game integration/save support planned for 0.2 |

German and English editions can share a physical format while differing in text, league contents, executable layout and currency. Select the edition through the supplied installation; do not rename files or patch a save header to simulate another version. Original English V2.20 save import is a separate compatibility limitation from English installation support.

The loaders and accessors are authoritative for supported bounds and validation. Unknown fields are preserved where required by save compatibility; an unexplained value is not an extension point. File offsets and record strides here describe formats, not stable application API names.
