# OpenBMH Classic 1.0.9

This release aligns the engine's hard-coded rules with BMH 2026-27 DIAMOND
Edition 3.7. It requires the player's own game installation. Extended
development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Why

The engine had been checked against an older Diamond build whose executable
differs from 3.7 in about 2,700 bytes (50 code ranges, the Diamond patch area
and 64 data bytes). Rules from v2.24, an older Diamond and 3.7 were therefore
mixed. The routines of the real 3.7 executable were run in an emulator and
compared with the engine; every difference found was ported and documented.

## Changes since 1.0.8

- **Injuries and cards.** New injury tables (thresholds and durations), the
  injury history factor 1.25, doctor and insurance caps, and the foul, booking
  and red-card chances of 3.7.
- **Economy.** Credit limit 8,000,000, transfer fee base, DFB fee, advertising
  base, start capital, bankruptcy limit, championship and cup premiums,
  periphery prices, contract talk limit and training injury range.
- **Weekly processing.** Retirement chance, side rating rhythm for human and
  computer managers, facility values, per-visitor income and the season
  ticket price.
- **Youth academy.** 3.7 generation with its monthly budget gate. An age check
  was inverted in the port (older players were recycled the wrong way round);
  this is fixed.
- **Season end.** Retirement and leave rules for the player slots.
- **Live match statistics.** The chance counters at the bottom of the live
  matchday screen counted a chance as soon as the engine logged it, before the
  presentation started. They are now held back like the score and appear with
  the result of the chance.

## What was measured, what was derived

Compared with the 3.7 executable without any difference (boundary values and a
deliberately wrong result per rule that must fail): random generator, market
value, bank annuity, club mood and flags, cup tie order, advertising offers,
bid rating, poaching, facility ageing and damage, manager offers, weekly
statistics, familiarity decay, squad training, daily interest, youth academy,
month change, new transfer list entries, and injuries including doctor and
insurance (932 runs).

Derived from the constants of the 3.7 code but **not** measured in the
emulator: foul and card chances in a match, and several economy numbers.
Not covered: the complete weekly processing routine, and the amounts of newly
created transfer bids (the original reads a stale stack value there; documented
in the source as a known gap).

## Open points

Reported by the Diamond author and not changed here: the bug in "fetch player
from transfer market / check offers" (exact steps still to be reproduced).
Earlier open points remain: the
"player card: original atlas bounds" error (#77), sporadic double clicks (#58)
and the macOS match scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.
