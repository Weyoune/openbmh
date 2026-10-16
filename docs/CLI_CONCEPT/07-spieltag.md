# Matches and season boundaries

The console's stepped clock allows managers to inspect a live match and intervene through the normal tactics and substitution actions. Start with the current status, own pairing and statistics; a global match window can exist on a cup day when a particular manager has no fixture.

`Step(minutes)` advances a requested interval. `Run(Halftime)` requests the half-time boundary, and `Run(End)` requests completion. These are shared clock commands: with multiple seats, every required seat must submit readiness. Place the clock command last in the block. Read the returned minute and phase before the next request; never ask for half-time after it has already passed. If another seat advances to an intermediate stop, inspect that actual state rather than assuming the previous request's target was reached.

Only intervene in a match that actually includes the manager's team. A manager without a fixture still participates in the shared clock barrier. Do not manufacture substitutions or tactical mutations merely because another team's match is running.

Status can report `matchday running, minute N` without a new `# Window: Match` header. Questions can interrupt a clock request and belong to another manager. Follow the question ownership rules in [syntax](01-grundlagen.md) and the [client introduction](../CONSOLE_CLIENTS.md).

At a season-end desktop, inspect the completed table and financial/squad position before continuing. A historical final table is not itself a new year transition. `State(Desk)` supplies the phase and season index for clients that need to track the boundary. Finite careers can terminate in `GameEnd`; do not keep submitting advance commands after completion.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `Match`

| Command | Parameters | Purpose |
|---|---|---|
| `Step` | [minutes:int=1] | let the clock run n minutes (default 1) |
| `Run` | [until:enum(Halftime,End)=End] | run to half time or to the end of the day |
| `Pause` | – | Pause. See current Help for behaviour and bounds. |
| `Resume` | – | Resume. See current Help for behaviour and bounds. |
| `ShowMatchStats` | [match:int] | the scoreboard: score, chances, cards, spectators, weather |
| `Watch` | match:int | Watch. See current Help for behaviour and bounds. |
| `Substitute` | out:Player, in:Player | a substitution: a player leaves, a bench player comes on |
| `SetFormation` | slot:int(1..8) | not during a match (move single players with MovePlayer) |
| `MovePlayer` | player:Player, depth:int, lane:int | place a starter on another position for the rest of the match (from minute 3) |
| `SetTactic` | item:int(0..7), value:int | change a tactic setting during the match (item 0 ends the assistant's D/O control) |
| `ShowTactics` | – | the eight tactic settings of your team in this match |
| `SetMarking` | own:Player, opponent:Player | Set Marking. See current Help for behaviour and bounds. |
| `SetOrder` | player:Player, aspect:enum, value:int | Set Order. See current Help for behaviour and bounds. |
| `Open` | window:enum(Lineup,Tactics,Table,Fixtures,Settings) | Open. See current Help for behaviour and bounds. |
| `ShowSquad` | – | your team on the pitch and on the bench |

### `Desktop|Lineup|Friendlies`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowNationalSquad` | – | Show National Squad. See current Help for behaviour and bounds. |
| `NationalSquadAdd` | player:Player | National Squad Add. See current Help for behaviour and bounds. |
| `NationalSquadRemove` | player:Player | National Squad Remove. See current Help for behaviour and bounds. |

### `GameEnd`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowHighScore` | – | Show High Score. See current Help for behaviour and bounds. |
| `NewGame` | duration:int(1..5), difficulty:int(1..10), managers:int(1..4), mode:int(1..3), league:bool, world:bool | New Game. See current Help for behaviour and bounds. |
| `LoadGame` | filename:string | Load Game. See current Help for behaviour and bounds. |

### `Desktop`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSportschau` | [part:enum(Results,TimeBar,BestEleven,MatchdayOverview)] | Show Sportschau. See current Help for behaviour and bounds. |
