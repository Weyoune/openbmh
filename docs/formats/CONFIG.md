# Legacy installation configuration

`SOUND.CFG` and `PFAD.MAN` belong to the DOS installation. OpenBMH uses host SDL audio and its data-directory configuration instead of DOS ports, IRQs, DMA channels or path-prefix files.

## `SOUND.CFG`

The 30-byte file begins with the nine-byte `ALPTRAUM!` signature. A marker byte and two words precede two eight-byte device records. Each record contains four little-endian words representing the enabled state and legacy Sound Blaster configuration. These values describe the old machine, not valid SDL device settings.

## `PFAD.MAN`

This short legacy text file supplies a DOS path prefix. OpenBMH accepts a game location through startup selection or `--data`. Do not copy machine-specific DOS configuration into a new OpenBMH configuration file.
