# Measurement against Diamond 3.7: matchdays, weekly processing, month change

OpenBMH Classic follows the game and match engine of BMH 2026-27 DIAMOND Edition 3.7. Besides the routine-by-routine
comparisons (see the 1.0.9 release notes) the engine is checked against **recordings of the real 3.7** running in an
emulator (DOSBox-X). A recorder reads the emulator's memory without touching the game: at every match minute, at every
change of the calendar day and at every pause it stores the random number states (game and C library generator) and all
tables the engine reads (squads, player words, team flags, cards, managers, stock market, transfer list, mailbox, ...).
The port replays the same turn from the save game that precedes it and is compared at every one of these checkpoints.

The recorder and the automation that operates the original are not part of this repository (they need the original
program); the checker is `tests/week_check.cpp`.

## Scope of the November 2026 series

- Eleven recorded "Weiter" turns of one career, from the first Saturday in August to mid October (game days 38 to
  108; the turn from day 52 to 59 was played without recording), recorded from the real 3.7 and replayed by the
  port. The human manager's club played every matchday (league, and the cup tie of the first turn). Incoming transfer
  offers were declined.
- **Match phase:** 1,292 recorded match minutes, every one inside the interval of random draws that the port passes
  through, 0 outside. The final whistle, the stoppage time and the first extra-time pass match in every turn.
- **Weekly processing:** nine runs (game days 43, 50, 64, 71, 78, 85, 92, 99, 106) of the routine ported from the
  original. At the desk after each turn the random states and **all tables are identical** to the recording.
- **Month change:** two runs (1 September = day 62 and 1 October = day 92, the latter together with the weekly
  processing). Identical at the desk afterwards.
- Training, the stock market, the matchday preparation and the kick-off of the next matchday are part of the same turns
  and match as well (the match groups built by the port equal the first minute snapshot of the next matchday).

## How to read the "DIFF" lines of the checker

The recorder takes a day snapshot when the date changes, i.e. **inside** the original's processing of that day; the
port's state belongs to the end of the processing. The checker therefore prints such checkpoints as "late snapshot ...
draws ahead of the port's state, not counted". They are not differences of the engine: the checkpoint after them (and the
desk at the end of the turn) is identical in every recorded turn. Nothing else was left unexplained: a checkpoint of the
first turn that reads the manager records after saving differs in one byte of one manager record and is equal again at
the desk.

## Not covered

- Only one career and one human manager. Contract expiry on 1 July, the winter break, the season end and the
  transitions between seasons were not recorded in this series (the 1.0.9 comparisons cover parts of them routine by
  routine).
- Offers made by the weekly processing to the human manager (new transfer bids): the amounts depend on a stale stack
  value in the original and are not reproduced (documented in the source).
- Foul and card chances are derived from the constants of the 3.7 code and show no difference in the 1,292 recorded
  minutes, but a deliberate comparison of the single routine was not made.
