# Indexed bitmap: `.VGA`

`src/formats/vga.cpp` decodes eight-bit palette indices from a continuous run-length stream. Coordinates use a top-left origin.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u16 | Width |
| 2 | u16 | Height |
| 4 | u32 | Encoded length, including one trailing byte |
| 8 | bytes | Encoded pixel stream |

For a control byte `c >= 0x80`, repeat the following byte `c - 0x7f` times. Otherwise copy the next `c + 1` literal bytes. Decoding continues across row boundaries until `width * height` indices have been produced. The loader rejects truncated data and requires exactly one byte to remain within the declared stream afterwards. A run exceeding the pixel count is clipped by the decoder.

The default colours come from [M3PC.COL](COL.md). Certain full-screen resources append a 768-byte palette after the encoded stream; callers loading those resources must select that palette separately. `decode_vga` itself returns indices and does not interpret appended palette data.

Crest sheets use 38 by 38 pixel cells, eight per sheet. For a nine-bit crest ID, the file number is `id >> 3` and the horizontal cell is `id & 7`. This describes the lookup scheme rather than any particular copyrighted crest or sheet content.
