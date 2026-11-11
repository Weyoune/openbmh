# Architecture

OpenBMH is a C++20 engine with graphical and console frontends. The implementation reads externally supplied game data; it does not execute the original program. See [file formats](formats/README.md) for the compatibility representations and [roadmap](ROADMAP.md) for the planned typed model.

## Layers

| Directory | Responsibility |
|---|---|
| `src/formats` | Binary readers, explicit-width records, palettes, fonts, images, text, executable-data loading and save containers |
| `src/game` | Game state, random generators, career setup, scheduling, match simulation, accounting and weekly/monthly/seasonal progression |
| `src/app` | CareerSession, shared actions, revisions, ownership, transactional publication and rollback |
| `src/ui` | View projections, rendering and input interpretation for game screens |
| `src/cli` | Command language, structured protocol, console windows and multi-client seat routing |
| `src/audio` | Bank decoding, synthesis, playback and cues, without authority to change gameplay |
| `src/launch` | Locating user data, archive extraction, edition normalisation and file selection |

`bmhcore` contains the format and game layers. `bmhui`, `openbmh_audio` and the application session provide reusable pieces above it. Frontends should translate actions and display results; they should not introduce independent transfer, calendar or financial rules.

## Session boundary

`CareerSession` owns the live game, the two random streams and progression state. Requests identify the manager and the revision they observed. Shared actions validate that identity, stage state and random effects, and publish supported results together. Rejected requests must not leave partial transactions.

Some calendar operations need synchronous human decisions. Their presenters own a staged state until the question or editor returns. A console worker hands control to the input thread while paused; exactly one thread operates on that transaction at a time. Cancellation, failure and process shutdown must release the owner and preserve the transaction's rollback contract. A view must not read live state as though it were the paused staged state.

## Calendar and matches

The turn driver moves between manager desks, match clocks and calendar events. Weekly, monthly and seasonal processing can produce decisions as well as simulation results. Match groups contain the day's competitions; a manager can intervene only in a group involving that manager's team. A multi-client match clock advances at a barrier shared by all seats.

## Compatibility state

The current game model combines interpreted vectors with fixed-layout compatibility blocks. Identifiers are not interchangeable: a club's stable identity, current runtime team index, squad row and player id represent different things. Guard each at its boundary. Fixed offsets belong to file/layout code; preserve integer widths and signedness when porting calculations.

Legacy saves serialize blocks in a fixed order and store name pools separately. Runtime UI owners, pending input and frontend capabilities are not additional save fields. See [FBM](formats/FBM.md), [state records](formats/STATE.md) and [RNG](RNG.md).

## World layout

The dimensions of the game world are described in one place, `src/game/world_layout.hpp`, instead of as numbers spread over the engine:

- `WorldLayout` holds the team id ranges (league clubs, amateur clubs, foreign cup clubs without squads, the pseudo team "no club", national teams) and the squad slot table with the player pools behind it.
- `LeagueSystem` holds the league structure of the loaded game: league sizes, calendar groups, promotion ladder, European places and the cup structure. The session installs it from the tables of the game version, so the same engine runs the German and the English data.
- `src/game/records.hpp` names the sizes of the fixed records (manager record, tactics board, club record).

OpenBMH Classic only ever uses `classic_world_layout()`, which returns the numbers of the original; a test compares them and the squad table with the former literals. Code asks `world_layout()` and `league_system()` rather than writing 138 or 168.

This is a parameterisation, not yet a larger world. Still fixed, and to be lifted for an edition with more leagues:

- the number of leagues (`kLeagueCount`) and of calendar groups (eight competition bits),
- the results block (one fixed stride per league, `kLeagueResultsStride`) and the other save blocks sized for the Classic world; the save format is unchanged and a larger world needs a format of its own,
- the id of "no club" as a compile-time constant (`kNoTeam`), five manager records, and some stack tables sized for the Classic club count,
- the per-club tables of the loaded state, whose sizes are checked against the Classic numbers.

## Public interfaces and validation

The [console reference](CLI_CONCEPT/README.md) documents window commands. [protocol.md](protocol.md) describes structured requests, and [CONSOLE_CLIENTS.md](CONSOLE_CLIENTS.md) explains safe client ownership and batching. Prefer shared projections/actions when adding another frontend.

Use synthetic data for public tests and locally supplied installations only for data-dependent checks. Renderer tests, simulation tests and target-platform runtime checks answer different questions; document what each validates. See [CONTRIBUTING.md](../CONTRIBUTING.md).
