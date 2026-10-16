# Palette: `M3PC.COL`

The file contains 256 RGB triplets: 768 bytes, with each channel a six-bit VGA DAC value. `load_palette` in `src/formats/palette.cpp` requires at least that size and ignores trailing data.

A channel is masked with `0x3f` and expanded to eight bits using `(v * 255 + 31) / 63`. Rendering uses opaque alpha unless the drawing operation supplies transparency separately. No palette index is inherently transparent.

Some [VGA](VGA.md) images supply a separate palette. The same channel conversion applies. `Palette::nearest` is an implementation helper for colour matching, not an additional file field.
