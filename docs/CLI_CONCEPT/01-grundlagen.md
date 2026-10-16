# Console syntax and responses

The console runs command blocks against one career session. Start it with `openbmh_cli --console --data "path/to/game" --saves "path/to/saves"`. This language is distinct from the narrower JSON request protocol used without `--console`.

## Input

Put one command on each line and finish the block with `go.` on a line of its own. `clear.` discards an unexecuted block. Blank lines and lines beginning with `//` are ignored. Commands execute in order; a failure stops the remaining commands. Earlier successful actions remain committed: a block is not one transaction.

Command syntax is `[@player] Name(argument, name=value)`. The player prefix uses seats 1–4. Names ignore case, dots and underscores. Arguments can be positional, followed by named arguments; quote strings containing spaces, commas or punctuation. Escape a literal quote or backslash with a backslash. Integer values can use underscores as separators. Boolean values include `true`/`false` and `on`/`off`. Parameter types and accepted enum values are checked before an action is dispatched.

Use `id:n` for a player ID where accepted, `slot:n` for a current squad position, and `club:n` for a master club reference. Rows and slots are temporary references. Player IDs, master club indices, runtime team IDs and name-pool identity keys are different domains. Obtain the appropriate reference from the current list or state view.

## Responses

Text lines carry a leading class: `#` status, `>` command option, `|` table row, `+` success, `!` error, `?` question, `i` information, `~` match event and `c` chat. Status headers are emitted when needed; absence of a new header does not imply that the window or match has ended. Read the final status and all intervening question/error lines.

`--json` in console mode emits structured line records with `class`, `window`, `player`, `text` and optional `data`. This is the structured representation of console output, not the standalone request protocol. Prefer it over parsing translated prose. Legacy game strings can still expose encoding limitations; retain raw bytes if reporting a decoder problem.

## Questions and ownership

A question has an ID, a recipient and listed options. Answer with `Answer(id, option)`, using the actual ID and option from the response. Only its owner can decide it. A question can occur during another manager's turn or while advancing the shared match clock. `GoBack` can select a question's default or cancel an editor; it is not a harmless substitute for an explicit answer.

`ListOptions` and `Help` describe the current build. Common failures include `E_BAD_ARGUMENT`, `E_NOT_IN_WINDOW`, `E_NOT_YOUR_TURN`, `E_PENDING_QUESTION`, `E_RULE`, `E_MODE_LOCKED`, `E_PHASE_LOCKED`, `E_IO` and `E_ENGINE_GAP`. A recognised name does not guarantee that every mode or path is implemented. Preserve an unexpected error and inspect state before deciding whether to retry.

The [client introduction](../CONSOLE_CLIENTS.md) covers transport, turn ownership, safe batching and replay.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `*`

| Command | Parameters | Purpose |
|---|---|---|
| `ListOptions` | [command:string \| all] | commands possible here; ListOptions(all) also shows locked ones |
| `GoBack` | – | right mouse button: one level back, otherwise nothing |
| `Chat` | [to:int], text:string | message to all, or to one player (0 = only the log) |
| `ChatLog` | [count:int=20] | the last chat messages |
| `Answer` | id:int, option:enum | answer the open question |
| `Status` | – | window, player and a short overview |
| `State` | view:enum(Desk,Table,Lineup,Mailbox,Bank,Market,Advertising,Contracts,Stadium,Toto,Training,Camp) | the full projection of a view as structured (JSON) data |
| `Advice` | – | situations that need a decision now: form, squad, contracts, advertising, money |
| `Scoreboard` | – | all human managers: league, place, points, balance, strength of the best eleven |
| `Help` | [command:string] | command reference (all windows) |
| `Quit` | [force:bool=false] | end the session (asks first during a game) |
| `HarnessState` | [path:string] | Harness State. See current Help for behaviour and bounds. |
| `HarnessHash` | – | Harness Hash. See current Help for behaviour and bounds. |
| `HarnessInvariants` | – | Harness Invariants. See current Help for behaviour and bounds. |
| `HarnessAutopilot` | player:int(1..4), policy:enum(Default,Saver,Spender,Random,Off) | Harness Autopilot. See current Help for behaviour and bounds. |
| `HarnessFast` | matchdays:int, [until:enum(Matchday,Season,GameEnd)] | Harness Fast. See current Help for behaviour and bounds. |
| `HarnessCoverage` | – | Harness Coverage. See current Help for behaviour and bounds. |
| `HarnessSeed` | – | Harness Seed. See current Help for behaviour and bounds. |
| `HarnessExpect` | text:string | Harness Expect. See current Help for behaviour and bounds. |
