# Text database: `TEXTD.BLK` / `TEXTE.BLK`

`src/formats/textd.cpp` reads a seven-byte header followed by consecutive NUL-terminated legacy strings.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u8 | Edition-specific identifier |
| 1 | u16 | Declared buffer size |
| 3 | u32 | Header field with no application-level interpretation |
| 7 | bytes | String buffer |

The loader indexes the first 1,382 strings and rejects a shorter database. Declared buffer size can differ from the actual payload; the loader takes the remaining file bytes rather than trusting it as a stream length. Each stored offset is relative to the start of the string buffer, not to the file or a UTF-8 result.

Strings are converted through the legacy character decoder when displayed. Currency labels and other edition content come from the installation. The text database is not copied into a savegame; the name pools stored in [FBM](FBM.md) are separate.

This document intentionally describes structure only. A distributable replacement must provide independently licensed text; copying or translating the supplied game's text database does not create a new license for it.
