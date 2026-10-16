# Starting or loading a career

Startup moves from `NewGame` to `NewGame.Select`, then asks each selected manager to confirm their choices. Use `ShowOptions` and `ListTeams` to obtain the current values and master club references. Do not assume that the same numeric club identifies the same content in German and English installations.

`NewGame` sets the duration, difficulty, manager count, mode and league/world choices. Team selection records a manager name, club, starting league, strength band and portrait. `Confirm` opens a question; answer its actual ID explicitly. With several managers, finish each selection before the first desktop appears.

`OpenLoadBrowser` / `LoadGame` enter normal save loading. Paths are resolved against the configured save browser. The loader validates format/version compatibility; renaming an unsupported save or altering its header does not make it supported. Adding a manager later uses `NewPlayer` and remains subject to the running career's rules.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `NewGame`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowOptions` | – | show the options with their current values |
| `NewGame` | duration:int(1..5), difficulty:int(1..10), managers:int(1..4), mode:int(1..3), league:bool, world:bool | set all options and continue (= right mouse) |
| `SetDuration` | duration:int(1..5) | 1-4 seasons, 5 = endless |
| `SetDifficulty` | difficulty:int(1..10) | number of balls |
| `SetManagers` | managers:int(1..4) | human managers |
| `SetMode` | mode:int(1..3) | range of desk functions |
| `SetLeagueMode` | on:bool | league mode on/off |
| `SetWorldMode` | on:bool | world cup mode on/off |
| `OpenLoadBrowser` | – | open the load browser |
| `LoadGame` | filename:string | load a saved game |

### `NewGame.Select`

| Command | Parameters | Purpose |
|---|---|---|
| `ListTeams` | [league:int \| tab:enum] | clubs of a picker tab (Bundesliga ... AdditionalB) |
| `ListPortraits` | – | the twelve portraits and who took them |
| `ListStrengths` | – | the three club strengths |
| `ShowSelection` | – | the choices of this player |
| `SetManagerName` | name:string(1..20) | the manager's name (1-20 characters) |
| `SetClub` | club:Club | the club: club:<n> from ListTeams or "name" |
| `SetStartLeague` | league:enum(Bundesliga,SecondLeague,RegionalA,RegionalB,Oberliga) | the league to start in |
| `SetStrength` | strength:enum(Upper,Middle,Lower) | the club's strength |
| `SetPortrait` | portrait:int(0..11) | the manager's portrait |
| `SelectTeam` | managerName:string, club:Club, startLeague:enum, strength:enum, portrait:int | set all five and ask for confirmation |
| `Confirm` | – | show the summary and ask "all details correct?" |

### `NewGame.Select.Confirm`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSummary` | – | the choices of this player |

### `NewPlayer`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowNewPlayerOptions` | – | Show New Player Options. See current Help for behaviour and bounds. |
| `SetManagers` | managers:int(current..4) | Set Managers. See current Help for behaviour and bounds. |
| `SetDifficulty` | difficulty:int(1..10) | Set Difficulty. See current Help for behaviour and bounds. |
| `Confirm` | – | Confirm. See current Help for behaviour and bounds. |
