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

- 73 recorded "Weiter" turns of one career, from the first Saturday in August 2026 (game day 38) through the whole
  first season, the season end on 1 July 2027 (day 365) and most of the second season up to April 2028 (day 645).
  The human manager's club played every matchday (league, cup and the indoor tournament). Incoming transfer offers
  were declined.
- **Match phase:** 7,642 recorded match minutes, every one inside the interval of random draws that the port passes
  through, none outside. Final whistle, stoppage time and the first extra-time pass match in every turn, in the second
  season as well.
- **Weekly processing:** 70 runs of the routine ported from the original (days 43 to 645). At the desk after each turn
  the random states and **all tables are identical** to the recording.
- **Month change:** 17 runs (1 September 2026 to 1 April 2028, among them the change of the year and the summer break).
  Identical at the desk afterwards.
- **Season end:** one run (day 365 with the last weekly processing and the month change, from the last matchday of the
  season on 8 May 2027 to the desk of 1 July 2027). Identical, all tables.
- **Indoor tournament:** eleven rounds (see below).
- Training, the stock market, the matchday preparation and the kick-off of the next matchday are part of the same turns
  and match as well (the match groups built by the port equal the first minute snapshot of the next matchday).

Of the 73 evaluated turns 71 are identical at the desk (random states and all tables; the transfer list is shown
separately, see below). The other two are explained:

- One differs in a single lineup flag of one player slot that the original clears when the manager's lineup page is
  shown (a routine of the user interface, not of the engine); the recording passed through that page.
- The last round of the indoor tournament ends with the tournament processing of the original (prizes, next
  fixtures); the checker replays only the playing part of an indoor round, so that part is not compared.

## Indoor tournament

All eleven recorded rounds play their 40 minutes with the same random draws as the original. The six strength values
of the side records at the kick-off first differed by 1 to 4 points in about half of the rounds (the port mostly
higher). Cause (issue 82, resolved): the strengths depend on the **chance weather**, a runtime value that the
original keeps from the last chance of the round before (rain above 2 or a temperature below 0 lowers players
whose condition is below their technique). A savegame does not hold it, and the checker starts every turn from a
savegame, so it had rain 0 / temperature 0. With the weather of the previous turn's last chance
(`WEEK_PREV_WEATHER=rain,temperature`, taken from the "last chance weather" line of the previous check) the kick-off
of **all eleven rounds is identical**, bytes 0 to 5 of every side. A running game keeps the weather in memory
like the original, so this was a gap of the check and not of the engine. The desk after the round is identical in
ten of eleven rounds (the eleventh is the tournament end above).

## How to read the "DIFF" lines of the checker

The recorder takes a day snapshot when the date changes, i.e. **inside** the original's processing of that day; the
port's state belongs to the end of the processing. The checker therefore prints such checkpoints as "late snapshot ...
draws ahead of the port's state, not counted". They are not differences of the engine: the checkpoint after them (and the
desk at the end of the turn) is identical in every counted turn. The transfer list is compared but not counted (the
amounts of new bids depend on stale data in the original, documented in the source); a few desks show a handful of
bytes there and the line says so.

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
- A second season end and later seasons; the end of the indoor tournament.
