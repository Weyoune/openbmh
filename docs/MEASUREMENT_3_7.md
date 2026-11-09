# Measurement against Diamond 3.7: matchdays, weekly processing, month change, season end

OpenBMH Classic follows the game and match engine of BMH 2026-27 DIAMOND Edition 3.7. Besides the routine-by-routine
comparisons (see the 1.0.9 release notes) the engine is checked against **recordings of the real 3.7** running in an
emulator (DOSBox-X). A recorder reads the emulator's memory without touching the game: at every match minute, at every
change of the calendar day and at every pause it stores the random number states (game and C library generator) and all
tables the engine reads (squads, player words, team flags, cards, managers, stock market, transfer list, mailbox, ...).
The port replays the same turn from the save game that precedes it and is compared at every one of these checkpoints.

The recorder and the automation that operates the original are not part of this repository (they need the original
program); the checker is `tests/week_check.cpp`.

## Scope of the November 2026 series

- 51 recorded "Weiter" turns of one career, from the first Saturday in August 2026 (game day 38) through the whole
  first season, the season end on 1 July 2027 (day 365) and the first third of the second season (about day 533).
  The human manager's club played every matchday (league and cup). Incoming transfer offers were declined.
- **Match phase:** 5,912 recorded match minutes, every one inside the interval of random draws that the port passes
  through, none outside. Final whistle, stoppage time and the first extra-time pass match in every turn, in the second
  season as well.
- **Weekly processing:** 53 runs of the routine ported from the original. At the desk after each turn the random states
  and **all tables are identical** to the recording.
- **Month change:** 13 runs (1 September 2026 to 1 December 2027, among them the change of the year and the two in the
  summer break). Identical at the desk afterwards.
- **Season end:** one run (day 365 with the last weekly processing and the month change, from the last matchday of the
  season on 8 May 2027 to the desk of 1 July 2027). Identical, all tables.
- Training, the stock market, the matchday preparation and the kick-off of the next matchday are part of the same turns
  and match as well (the match groups built by the port equal the first minute snapshot of the next matchday).

Of the 51 evaluated turns 50 are identical at the desk. The remaining one differs in a single lineup flag of one
player slot that the original clears when the manager's lineup page is shown (a routine of the user interface, not of
the engine); the recording passed through that page.

## How to read the "DIFF" lines of the checker

The recorder takes a day snapshot when the date changes, i.e. **inside** the original's processing of that day; the
port's state belongs to the end of the processing. The checker therefore prints such checkpoints as "late snapshot ...
draws ahead of the port's state, not counted". They are not differences of the engine: the checkpoint after them (and the
desk at the end of the turn) is identical in every counted turn. The transfer list is compared but not counted (the
amounts of new bids depend on stale data in the original, documented in the source); where it differs at a desk the
line says so.

## Recordings that were not used

Four turns of the series were dropped, none because of a difference of the engine: two were disturbed by the operation
of the original (a mouse click that listed two players for transfer; a turn that clicked through several matchdays
and did not keep the recorder running), two belong to the start of the second season before the first matchday
(the season start screens with the Super Cup; no minutes were recorded). A first recording of the season end failed
because the recorder gave up after 60 s of the long end-of-season processing; recorded again with a longer wait it
matches.

## Not covered

- Only one career and one human manager. Contract expiry on 1 July and the other manager decisions at the season end
  were not driven by a human in this series.
- The start of the second season between the desk of 1 July and the first matchday (Super Cup, season start screens)
  was played without recording.
- Offers made by the weekly processing to the human manager (new transfer bids): the amounts depend on a stale stack
  value in the original and are not reproduced (documented in the source).
- Foul and card chances are derived from the constants of the 3.7 code and show no difference in the recorded
  minutes, but a deliberate comparison of the single routine was not made.
- A second season end and later seasons.
