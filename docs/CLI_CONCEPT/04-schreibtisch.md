# Desktop, calendar and saved games

`Desktop` is the management turn. Read `ShowDesk`, `ShowDate`, `ShowFinance`, `ShowOpponent` or `State(Desk)` before making decisions. `Open(window)` navigates to an available management window. Restrictions depend on game mode, manager role, phase and pending questions.

`Kickoff` hands control to the next manager or starts calendar processing once all required managers have finished. It can produce information, questions, a match window, a season boundary or the next desktop. Put it last in a batch and inspect its complete response.

`Calendar` reads the current schedule. `Mailbox` lists messages and supports normal keep/delete actions. Message rows are snapshot references; refresh them after a mutation. `LoadGame`, `SaveGame` and `NewGame.LoadGame` share browser commands. Select a file and use the normal `Save` action; resolve overwrite/delete questions explicitly. Save only a committed state accepted by the session.

Desktop layout commands are listed for discoverability. Some legacy layout operations can return `E_ENGINE_GAP`; appearance in this index is not a claim that every editor function is available.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `Desktop`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowDesk` | – | all displays of the desk |
| `ShowDate` | – | date, weekday, season day, league round |
| `ShowFinance` | – | balance, savings, ticket price |
| `ShowFanStanding` | – | standing with the fans |
| `ShowLamps` | – | the ten warning lamps |
| `ShowLastGames` | – | the last six games |
| `ShowOpponent` | – | the next game and opponent |
| `ShowPositionCurves` | [own\|opponent] | table positions per matchday |
| `ShowAutomation` | – | the five automations |
| `Open` | window:enum \| menu:int | open a window (click on its icon); see the lines below |
| `Key` | letter:enum(A..Z) | a key on the desk (G = Kickoff) |
| `SetTicketPrice` | price:int(8..25) | the ticket price (price scale) |
| `SetAutomation` | feature:enum(Lineup,Substitution,Tactics,Marking,Training), on:bool | switch one of the five automations (Lineup: a saved formation 1-8) |
| `Kickoff` | – | hand over to the next player or start the day |
| `OpenMailbox` | – | open the mailbox (Kicker) |
| `OpenDrawer` | – | open the menu bar |
| `PullPlug` | – | the plug: quit after a question |

### `Mailbox`

| Command | Parameters | Purpose |
|---|---|---|
| `ListMessages` | – | the messages of this manager (msg numbers for Keep/Delete) |
| `KeepMessage` | msg:int | keep a message / let it go (at most five) |
| `DeleteMessage` | msg:int | throw a message away |

### `Desktop.Drawer`

| Command | Parameters | Purpose |
|---|---|---|
| `ListIcons` | – | List Icons. See current Help for behaviour and bounds. |
| `MoveIcon` | window:enum, x:int, y:int | Move Icon. See current Help for behaviour and bounds. |
| `OpenFolder` | folder:int(1..3) | Open Folder. See current Help for behaviour and bounds. |
| `OpenTrash` | – | Open Trash. See current Help for behaviour and bounds. |
| `StoreIcon` | window:enum, folder:int(1..3) | Store Icon. See current Help for behaviour and bounds. |
| `RetrieveIcon` | window:enum | Retrieve Icon. See current Help for behaviour and bounds. |
| `SaveMenuDesign` | file:string | Save Menu Design. See current Help for behaviour and bounds. |
| `LoadMenuDesign` | file:string | Load Menu Design. See current Help for behaviour and bounds. |
| `SetTransparent` | window:enum, on:bool | Set Transparent. See current Help for behaviour and bounds. |
| `CloseDrawer` | – | Close Drawer. See current Help for behaviour and bounds. |

### `Calendar`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowCalendar` | [month:int(1..12)] | Show Calendar. See current Help for behaviour and bounds. |

### `LoadGame|SaveGame|NewGame.LoadGame`

| Command | Parameters | Purpose |
|---|---|---|
| `ListFiles` | – | List Files. See current Help for behaviour and bounds. |
| `ChangeDirectory` | name:string | Change Directory. See current Help for behaviour and bounds. |
| `ParentDirectory` | – | Parent Directory. See current Help for behaviour and bounds. |
| `SelectFile` | name:string | Select File. See current Help for behaviour and bounds. |
| `FileInfo` | name:string | File Info. See current Help for behaviour and bounds. |
| `Load` | [name:string] | Load. See current Help for behaviour and bounds. |
| `Save` | [name:string], [overwrite:bool=false] | Save. See current Help for behaviour and bounds. |
| `DeleteFile` | name:string | Delete File. See current Help for behaviour and bounds. |
