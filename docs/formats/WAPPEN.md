# IFF crest templates: `WAPPEN.LBM`

OpenBMH does not load this file. Its crest renderer uses indexed [VGA](VGA.md) sheets. No authorship or redistribution permission for templates found alongside a game installation is implied by their filename.

An IFF `FORM` of type `PBM ` can contain a `BMHD` bitmap header, an eight-bit RGB `CMAP`, optional application chunks and a `BODY` with chunky pixel indices. IFF lengths and header integers are big endian; chunks are padded to even boundaries.

For ByteRun1-compressed data, controls 0–127 copy the next `n + 1` bytes, controls 129–255 repeat the next byte `257 - n` times, and 128 is a no-op. This encoding differs from the game's VGA run-length stream. Adding a loader would require its own input validation and independently licensed test fixture.
