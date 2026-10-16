# Audio containers: `S/2000` through `S/2003`

`src/audio/container.cpp` validates the sound containers and decodes bounded PCM samples. `src/audio/original.cpp` implements the supported music sequencer, OPL instrument path, sample channel and effect dispatch. Audio resources and instrument values come from the user's installation; none are distributed here.

## Container structure

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `FORM` signature |
| 4 | u32, little endian | Declared length |
| 8 | 8 | `ALPTRAUM` type |
| 16 | Variable | Consecutive chunks |

Each chunk has a four-byte ID, a little-endian u32 payload length and its payload. Unlike standard padded IFF chunks, these chunks are adjacent without an alignment byte. The walker rejects overflows and requires the final chunk to end at the file boundary.

Music banks contain `HEDR`, `SONG`, `TRCK`, `PART`, `INST` and `SAMP` chunks. The effects bank uses `HEDR`, `SEFX`, `INST` and `SAMP`. The usual FORM length excludes the first eight bytes. The supported effects variant instead declares the whole file size; this exception is accepted only with its validated fixed chunk layout.

## PCM samples

A `SAMP` payload starts with two u16 words: a header field and the sample count. Each sample has a 16-byte descriptor followed by a shared unsigned eight-bit mono data area. Within a descriptor:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u16 | Start relative to the sample-data area |
| 2 | u16 | Length in bytes |
| 8 | u16 | Sample rate |

`decode_sample` checks the descriptor table and sample bounds, rejects empty samples and validates the rate. Conversion to signed sixteen-bit PCM is `(value - 128) * 256`.

## Sequencing and effects

`SONG`, `TRCK` and `PART` provide sequence order and timed note events; `INST` supplies instrument records. `OriginalSynth` schedules the supported register and sample operations using a deterministic audio tick. Nuked OPL3 renders the OPL-compatible register stream. This audio clock is separate from career random generators and match time.

The effects table has a four-byte prefix and sixteen-byte records. Instrument-backed effects and sample-backed effects use separate paths. `decode_original_cue` alone supports the direct sample-backed subset; this does not mean the full synthesiser lacks the instrument path. Music and effects settings control playback, while actual device availability belongs to the frontend. English audio compatibility remains subject to the release's experimental edition limits.

Sample-domain output checks establish decoder behaviour, not the timing or audible quality of a physical audio device. Platform listening and device checks are separate validation work.
