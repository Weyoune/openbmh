# Tables and information windows

Information windows expose league tables, fixtures, strengths, player cards, honours, other competitions, calendars and statistics. Choose a league or competition using the current view's options; avoid assuming that all editions have the same names or tournament structure.

A club's master identity and current runtime position differ. Use the identity returned for the intended action rather than reusing a row from another table. Sorting, range and home/away filters affect the current view; they do not rewrite the competition's saved result history.

`OpenPlayer` and `OpenTeamInfo` can create nested windows. `GoBack` closes the current level, subject to pending questions. Read-only reports can still be unavailable outside their supported mode or phase. Use `State(Table)` when a client needs an owned structured league table.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `LeagueTable`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowTable` | [league:int(0..8)] | the table (without league: the one shown) |
| `SetLeague` | league:int(0..8) | show another league |
| `ToggleEurope` | – | Toggle Europe. See current Help for behaviour and bounds. |
| `SetView` | view:enum(Total,Home,Away) | Set View. See current Help for behaviour and bounds. |
| `SetCriteria` | points:bool, goals:bool, goalsAgainst:bool | Set Criteria. See current Help for behaviour and bounds. |
| `SetRange` | all \| from:int, to:int | Set Range. See current Help for behaviour and bounds. |
| `SetPointsMode` | mode:enum(Normal,English) | Set Points Mode. See current Help for behaviour and bounds. |
| `ComputerTip` | – | Computer Tip. See current Help for behaviour and bounds. |
| `OpenFixtures` | – | the fixtures of this league |
| `OpenStrengths` | – | Open Strengths. See current Help for behaviour and bounds. |
| `OpenTeamInfo` | club:Club | Open Team Info. See current Help for behaviour and bounds. |

### `Fixtures`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowFixtures` | [league:int, matchday:int] | the page shown (optionally another league / matchday first) |
| `SetLeague` | league:int(0..8) | another league |
| `SetMatchday` | matchday:int | go to a matchday |
| `NextMatchday` | – | the next matchday (arrow) |
| `PreviousMatchday` | – | the previous matchday (arrow) |
| `FindGame` | home:Club, away:Club | Find Game. See current Help for behaviour and bounds. |
| `GamesOf` | club:Club | Games Of. See current Help for behaviour and bounds. |
| `Page` | n:int | Page. See current Help for behaviour and bounds. |
| `ShowPostponed` | – | the postponed games |
| `ComputerTip` | – | the computer's forecast for this page |
| `OpenTable` | – | the league table |
| `OpenStrengths` | – | Open Strengths. See current Help for behaviour and bounds. |
| `OpenTeamInfo` | club:Club | Open Team Info. See current Help for behaviour and bounds. |

### `Strengths`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowStrengths` | [league:int] | Show Strengths. See current Help for behaviour and bounds. |
| `SetLeague` | league:int(0..8) | Set League. See current Help for behaviour and bounds. |
| `SetCriteria` | fitness:bool, skill:bool, form:bool, morale:bool | Set Criteria. See current Help for behaviour and bounds. |
| `SetLines` | goalkeeper:bool, defence:bool, midfield:bool, attack:bool | Set Lines. See current Help for behaviour and bounds. |
| `SetZeroValue` | – | Set Zero Value. See current Help for behaviour and bounds. |
| `SortByAge` | on:bool | Sort By Age. See current Help for behaviour and bounds. |
| `SetPlayerFilter` | filter:enum(All,OnPitchOnly) | Set Player Filter. See current Help for behaviour and bounds. |
| `OpenFixtures` | – | Open Fixtures. See current Help for behaviour and bounds. |
| `OpenTable` | – | Open Table. See current Help for behaviour and bounds. |
| `OpenTeamInfo` | club:Club | Open Team Info. See current Help for behaviour and bounds. |

### `TeamInfo`

| Command | Parameters | Purpose |
|---|---|---|
| `OpenTeamInfo` | club:Club | Open Team Info. See current Help for behaviour and bounds. |
| `ShowTeamSquad` | [view:enum(A,B)] | the club's squad |
| `ShowTeamStatistics` | [scope:enum(Home,Away,Total)] | Show Team Statistics. See current Help for behaviour and bounds. |
| `SetBroadcast` | club:Club, mode:enum(Dots,Headphones,Camera) | Set Broadcast. See current Help for behaviour and bounds. |
| `ShowPlayer` | player:Player | a player of this club |
| `CloseTeamInfo` | – | back to where it was opened |

### `Achievements`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowAchievements` | – | Show Achievements. See current Help for behaviour and bounds. |
| `ShowCompetitionSeason` | competition:enum(League,DfbCup,European) | Show Competition Season. See current Help for behaviour and bounds. |

### `OtherGames`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowCompetition` | [competition:enum, round:enum, matchday:int] | Show Competition. See current Help for behaviour and bounds. |
| `SetCompetition` | competition:enum(DfbCup,UefaCup,ChampionsLeague,CupWinners,World,SuperCup) | Set Competition. See current Help for behaviour and bounds. |
| `SetRound` | round:string | Set Round. See current Help for behaviour and bounds. |
| `NextMatchday` | – | Next Matchday. See current Help for behaviour and bounds. |
| `PreviousMatchday` | – | Previous Matchday. See current Help for behaviour and bounds. |
| `CompleteView` | – | Complete View. See current Help for behaviour and bounds. |
| `OpenTeamInfo` | club:Club | Open Team Info. See current Help for behaviour and bounds. |

### `IndoorTable`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowIndoorTable` | – | Show Indoor Table. See current Help for behaviour and bounds. |
| `OpenTeamInfo` | club:Club | Open Team Info. See current Help for behaviour and bounds. |

### `TopScorers`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowScorers` | [list:enum(Goals,Assists,Score), scope:enum(League,Managers)] | the best ten: goals, assists or both; whole league or managers' clubs |
| `SetList` | list:enum(Goals,Assists,Score) | TORSCHÜTZEN / VORLAGEN / SCORER |
| `SetScope` | scope:enum(League,Managers) | GESAMTE LIGA / NUR MANAGER |

### `FairPlay`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowFairPlay` | – | Show Fair Play. See current Help for behaviour and bounds. |

### `HighScore`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowHighScore` | – | the high score list and your current score |

### `Statistics`

| Command | Parameters | Purpose |
|---|---|---|
| `OpenView` | view:enum(Opportunities,AllTimeTable,SeriesRecords,Transfers,WorldCup,Winners,Charts,Finances,MinuteStats) | Open View. See current Help for behaviour and bounds. |
| `ShowOpportunities` | [mode:enum(A,B,C,D)] | Show Opportunities. See current Help for behaviour and bounds. |
| `ShowAllTimeTable` | [country:enum(Bundesliga,France,England,Spain,Italy)], [mode:enum(Absolute,Relative)] | Show All Time Table. See current Help for behaviour and bounds. |
| `ShowSeriesRecords` | – | Show Series Records. See current Help for behaviour and bounds. |
| `ShowTransferHistory` | – | Show Transfer History. See current Help for behaviour and bounds. |
| `ShowWorldCupStats` | [nation:string] | Show World Cup Stats. See current Help for behaviour and bounds. |
| `ShowWinners` | [competition:enum(League,DfbCup,ChampionsLeague,CupWinners,WorldCup,EuroCup,IndoorMasters,SuperCup)] | Show Winners. See current Help for behaviour and bounds. |
| `ShowCharts` | kind:enum(TablePosition,GateIncome,Balance,Strength,AdvertisingIncome,Assets), [mode:enum(Absolute,Relative)] | Show Charts. See current Help for behaviour and bounds. |
| `ShowFinances` | – | Show Finances. See current Help for behaviour and bounds. |
| `ShowMinuteStats` | – | Show Minute Stats. See current Help for behaviour and bounds. |

### `*`

| Command | Parameters | Purpose |
|---|---|---|
| `ListLeagues` | – | leagues with number and size |
| `ListClubs` | [league:int] | List Clubs. See current Help for behaviour and bounds. |
