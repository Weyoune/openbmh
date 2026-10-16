# Switch settings: `.EIN` and saved settings

The legacy settings block is 180 bytes: twenty rows of nine bytes, without a header or encryption. The same block is retained in [FBM](FBM.md). OpenBMH's settings actions operate on the career's block; standalone `.EIN` import is not the save mechanism.

Rows are indexed 0–19 and columns 0–8 in code. On-screen row letters and one-based column labels are presentation conventions. For supported boolean cells, zero means enabled and one means disabled. Some cells carry high-bit flags, reserved values or numeric settings; do not normalise the whole block to booleans.

`src/game/state.hpp`, the settings action implementation and the settings projection define supported cells and their bounds. Availability can also depend on the current frontend's audio capabilities. A failed audio device is not represented by rewriting the stored music preference. Clients should use the projected availability and normal actions rather than editing raw settings bytes.
