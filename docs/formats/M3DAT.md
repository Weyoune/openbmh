# Formation coordinates: `M3.DAT`

The file is a 1,408-byte raw block containing four 352-byte manager slots. Each slot contains an outdoor and indoor set, each with eight 22-byte formation records.

A record stores eleven horizontal coordinates followed by eleven vertical coordinates. For a formation `f` and player slot `p`, the horizontal coordinate is at `set_base + f * 22 + p`; the vertical coordinate is eleven bytes later. The indoor set begins 176 bytes into the manager slot.

Career initialisation copies the first manager slot over the other three. Subsequently the manager-specific block is retained in [FBM](FBM.md). The semantic accessor is `GameState::formation_position` in `src/game/state.cpp`. Coordinates belong to the game's pitch grid, not directly to screen pixels.
