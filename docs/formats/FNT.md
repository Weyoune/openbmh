# Bitmap fonts: `.FNT`

`src/formats/font.cpp` loads a 28-byte little-endian header followed by a bitmap and glyph tables. The layout resembles an Amiga TextFont with pointer slots stored as placeholders.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u16 | Glyph height |
| 2 | u8 | Style |
| 3 | u8 | Flags; value 2 omits the spacing table |
| 4 | u16 | Nominal advance |
| 6 | u16 | Baseline |
| 8 | u8 | First character |
| 9 | u8 | Last character |
| 10 | u16 | Reserved |
| 12 | u32 | Bitmap pointer placeholder |
| 16 | u16 | Bytes per bitmap row |
| 18 | u16 | Reserved |
| 20 | u32 | Glyph-location pointer placeholder |
| 24 | u32 | Spacing pointer placeholder |

Let `n = last - first + 2`; the extra entry is the replacement glyph. After the header come:

1. `height * row_bytes` bytes of one-bit bitmap, most significant bit first.
2. `n` pairs of u16 bit offset and bit width.
3. `n` signed 16-bit advances, unless flags equal 2. Fixed-width glyphs use the nominal advance instead.

The loader requires the computed total size to match. Characters outside the supported range use the replacement glyph. Legacy character conversion is described in the [format index](README.md); use the supplied font with its matching text encoding.
