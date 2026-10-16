# Interpreting saved game state

The [FBM container](FBM.md) contains fixed-order state blocks rather than a self-describing object graph. `src/formats/savestate.cpp` defines the physical layout; `src/game/state.hpp` and `state.cpp` provide semantic accessors. Keep those implementations as the single source of truth when adding a field.

## Manager records

Five compatibility slots use 2,828-byte records. Not every slot represents a playable manager. Important offsets within each record include:

| Record offset | Type | Meaning |
|---|---|---|
| `0x0a8` | Fixed character area | Manager name |
| `0x290` | i16 | Runtime team ID; a sentinel represents no club |
| `0x314` | i32 | Cash balance in the engine's currency units |
| `0x748` | i32 array | First income accounting table |
| `0x85c` | i32 array | Second income accounting table |

Financial projections can include computed expenses; raw accounting tables do not by themselves reproduce the complete balance screen. A displayed currency conversion is not a change to saved arithmetic.

Other blocks retain desktop layouts, settings, formation coordinates, tactics, transfers, loan ledgers, calendar state, results, player words and name offsets. Loan duration is kept separately from a player's contract-byte sentinel; a sentinel must not be read as a real multi-century contract.

## Identity and schedule

Master club indices, club-name pool offsets, runtime team IDs, player IDs and squad rows are distinct identifiers. Initialisation can shuffle clubs between runtime positions. Promotion and relegation can change those positions again. Use a session's current projections to resolve identity rather than holding a runtime row across a season boundary.

The base league structure contains five eighteen-team and four twelve-team ranges. A result byte packs home goals in the high nibble and away goals in the low nibble; `0xcc` denotes an unplayed result. The schedule combines round templates with current calendar and competition state. Tables additionally incorporate deductions and selected competition rules; clients should request a table projection instead of deriving one from raw results alone.

Do not serialise frontend capabilities, pointers, callback owners or request IDs into this compatibility state. Session staging and rollback must include the associated runtime state and both [random streams](../RNG.md), not just selected byte arrays.
