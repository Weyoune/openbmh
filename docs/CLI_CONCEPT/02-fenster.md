# Windows and navigation

A console window is a session context, not a separate game process. `Open(window)` enters an available window from the desktop. Nested editors and information cards return to their parent through `GoBack`. Pending questions and staged engine operations can restrict navigation.

A dot in a window name denotes a specialised context, for example `Transfer.Negotiate`. It does not imply that a client can open it directly without selecting a player. Global commands use `*` in the catalogue. `ListOptions` is the reliable guide to the current context; an operation allowed on the desktop may be forbidden during a live match or another manager's turn.

The following identifiers occur in the command catalogue. Some are reserved or only available in particular modes. They are API names, not a guarantee of complete support for every original screen.

| Window | Reference |
|---|---|
| `NewGame` | [Commands](03-start.md) |
| `NewGame.Select` | [Commands](03-start.md) |
| `NewGame.Select.Confirm` | [Commands](03-start.md) |
| `NewPlayer` | [Commands](03-start.md) |
| `Desktop` | [Commands](04-schreibtisch.md) |
| `Mailbox` | [Commands](04-schreibtisch.md) |
| `Desktop.Drawer` | [Commands](04-schreibtisch.md) |
| `Calendar` | [Commands](04-schreibtisch.md) |
| `LoadGame` | [Commands](04-schreibtisch.md) |
| `SaveGame` | [Commands](04-schreibtisch.md) |
| `NewGame.LoadGame` | [Commands](04-schreibtisch.md) |
| `LeagueTable` | [Commands](05-information.md) |
| `Fixtures` | [Commands](05-information.md) |
| `Strengths` | [Commands](05-information.md) |
| `TeamInfo` | [Commands](05-information.md) |
| `Achievements` | [Commands](05-information.md) |
| `OtherGames` | [Commands](05-information.md) |
| `IndoorTable` | [Commands](05-information.md) |
| `TopScorers` | [Commands](05-information.md) |
| `FairPlay` | [Commands](05-information.md) |
| `HighScore` | [Commands](05-information.md) |
| `Statistics` | [Commands](05-information.md) |
| `Lineup` | [Commands](06-verein.md) |
| `Lineup.Pitch` | [Commands](06-verein.md) |
| `Lineup.Order` | [Commands](06-verein.md) |
| `Lineup.Marking` | [Commands](06-verein.md) |
| `Lineup.SetPieces` | [Commands](06-verein.md) |
| `PlayerCard` | [Commands](06-verein.md) |
| `PlayerCard.Statistics` | [Commands](06-verein.md) |
| `Tactics` | [Commands](06-verein.md) |
| `Training` | [Commands](06-verein.md) |
| `TrainingCamp` | [Commands](06-verein.md) |
| `Transfer` | [Commands](06-verein.md) |
| `Transfer.Negotiate` | [Commands](06-verein.md) |
| `Transfer.Search` | [Commands](06-verein.md) |
| `Transfer.Poach` | [Commands](06-verein.md) |
| `Transfer.Offers` | [Commands](06-verein.md) |
| `Contracts` | [Commands](06-verein.md) |
| `Contracts.Negotiate` | [Commands](06-verein.md) |
| `Bank` | [Commands](06-verein.md) |
| `Bank.Account` | [Commands](06-verein.md) |
| `Bank.Savings` | [Commands](06-verein.md) |
| `Bank.Shares` | [Commands](06-verein.md) |
| `Bank.RealEstate` | [Commands](06-verein.md) |
| `Bank.Credit` | [Commands](06-verein.md) |
| `Stadium` | [Commands](06-verein.md) |
| `Stadium.Periphery` | [Commands](06-verein.md) |
| `Stadium.Data` | [Commands](06-verein.md) |
| `Advertising` | [Commands](06-verein.md) |
| `Environment` | [Commands](06-verein.md) |
| `Balance` | [Commands](06-verein.md) |
| `Toto` | [Commands](06-verein.md) |
| `Friendlies` | [Commands](06-verein.md) |
| `Reschedule` | [Commands](06-verein.md) |
| `Match` | [Commands](07-spieltag.md) |
| `GameEnd` | [Commands](07-spieltag.md) |
| `Settings` | [Commands](08-einstellungen.md) |
