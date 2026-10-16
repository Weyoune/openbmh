# JSON Lines request protocol

`openbmh_cli` without `--console` offers a small JSON request adapter for a single `app::CareerSession`. It reads one JSON object per line and writes one response per line. `--text` is an interactive shorthand for this adapter. The broader window-oriented console and its `--json` output are a separate interface; use the [console reference](CLI_CONCEPT/README.md) for interactive questions, live matches and multiple manager clients.

```sh
openbmh_cli --data "path/to/game" --seed 1 --json
openbmh_cli --data "path/to/game" --seed 1 --json --log session.jsonl
openbmh_cli --replay session.jsonl --json
```

A request has `cmd` and the fields accepted for that command. Mutations carry an integer `manager` and the current `revision`. Responses contain `ok` and `revision`; errors add a stable `error` code and can include diagnostic `detail`. A rejected stale or foreign action must not mutate the projected state. Seed values are unsigned 32-bit decimal or hexadecimal values.

## Commands

| Command | Fields | Behavior |
|---|---|---|
| `help` | — | Machine-readable command and parameter list. |
| `new` | `options`, `managers` | Creates a career. This adapter accepts exactly one manager. `options` supports `duration` (0–5), `difficulty` (0–9), `mode` (0–3), `league`, `world`, and `managers:1`. The single manager has `name`, `master_club`, optional `start_league`, `strength` (0–2), and `portrait` (0–11). |
| `load` | `path` | Loads through `CareerSession`; a failed load leaves the current career active. |
| `save` | `manager`, `revision`, `path`, optional `overwrite` | Saves through the Session after checking the current manager and revision. |
| `status`, `desk` | — | Read the desktop projection and a compact status object. |
| `kickoff` | `manager`, `revision` | Calls the existing turn driver through `dispatch_kickoff`, advancing to the next desk. Returns state and available desk/audio event metadata. |
| `results` | — | Reads the owned results of the last completed event; `kickoff` also returns this object. A manager handoff or empty event has no new results. |
| `tactics`, `training` | — | Read current preparation values. Reading training does not initialize or recalculate it. |
| `tactics.set` | envelope, `index` (0–7), `value` | Sets a supported tactic; index 0 also disables the attitude assistant. |
| `training.initialize` | envelope | Performs the normal first-open initialization only when needed. |
| `training.type`, `training.area` | envelope, `index`, `value` (1–3) | Changes one of 4 types or 9 areas, validating supported plan limits and automation flags. |
| `training.intensity` | envelope, `row`, `value` (1–3) | Changes the player currently occupying the selected squad row. |
| `training.focus` | envelope, `row` | Cycles that player's training focus. |
| `training.auto` | envelope, `index` (0–4), `value` (0–1) | Toggles a training automation bit, validating the expected value and visible context. |
| `table` | `manager`, `league` (0–8) | Reads the selected full-season overall table in Engine rank order, with point deductions. Requires the active manager. |
| `mail` | `manager` (0–3) | Reads the selected existing manager's mailbox, sorted by date and physical slot. Does not mark, keep or delete messages. |
| `camp.focus` | envelope, `focus` (4 integers) | Dispatches a training-camp focus change. |
| `camp.book` | envelope, `resort` (0–7) | Books the selected camp. |
| `bank.deposit`, `bank.withdraw` | envelope, positive `amount` | Moves funds through the bank dispatch API. |
| `bank.shares.buy`, `bank.shares.sell`, `bank.tip` | envelope, `asset` (0–11), optional `count` | Uses the bank dispatch API. |
| `bank.fund.buy`, `bank.fund.sell` | envelope, `asset` (0–5), `count` (1–10) | Uses the bank dispatch API. |
| `stadium.open` | envelope | Opens the existing stadium projection/action. |
| `stadium.offer` | envelope, `object` (0–13) | Requests the existing periphery quote. |
| `stadium.build`, `stadium.decline` | envelope, `object` (0–13), `price` | Uses the existing stadium dispatch API. |
| `toto.submit` | envelope, `stake`, `tips` (11 values, each 0–2) | Submits through the existing Toto dispatch API. |
| `quit` | — | Ends the input loop. |

The `envelope` above means integer `manager` and `revision`. This adapter uses one manager,
slot 0; a different manager or stale revision is rejected. A failed `new` does not
create a Session. A successful `load` starts a new CLI revision sequence.

The text mode is intentionally narrower: `help`, `new <master_club> <quoted name>
[strength]`, `load <quoted path>`, `save <quoted path>`, `status`, `desk`, `kickoff`,
`camp.book <resort>`, `results`, `tactics`, `tactics.set <index> <value>`, `training`,
`training.initialize`, `training.type <index> <value>`, `training.area <index> <value>`,
`training.intensity <row> <value>`, `training.focus <row>`,
`training.auto <index> <value>`, `table <manager> <league>`, `mail <manager>`,
bank deposit/withdraw/share/fund/tip actions, and `quit`. `help`
prints parameter hints. Use JSON mode for all option fields, camp focus, stadium and Toto
requests, and for machine clients.

## Errors and scope

Malformed JSON, unexpected fields and invalid field types return `invalid_request`. Unknown operations return `unsupported_command`; recognised operations without a session adapter return `career_session_api_unavailable`. `help` describes the command set implemented by this adapter. An operation can also be rejected by its normal phase, mode, manager or rule checks. These outcomes must not be converted into successful client-side approximations.

This adapter does not expose the console's full squad, calendar, finance, live-match, question/answer or multi-seat interfaces. Its `new` operation requires one manager. A loaded career is subject to the session's actual continuation and revision checks; a successful read alone is not a claim that every later action is available in this adapter.

`table` returns owned club names, a club identity key, runtime team ID, ranks, games, wins/draws/losses, goals and points. Identity keys, runtime positions and master database indices are distinct. `mail` returns a snapshot of mailbox slots and recipient/keep masks. Keeping is not the same as marking read. Slot references can change when messages are compacted and are not persistent message IDs.

## Log and replay

`--log FILE` writes a header with protocol version, data location and seed, then canonical request/response records. `--replay FILE` repeats those requests and reports a mismatch. Replay can overwrite a path recorded by a save operation, so use an isolated writable save directory. Keep the same compatible binary and data when comparing a run.

The log contains supplied values and game responses, potentially including proprietary text or personal paths. It is not a distributable substitute for game data, nor a complete serialisation of every hidden session mechanism. Do not attach a full game log or save to a public issue; prepare minimal independently written reproduction steps.

Text errors use `ERROR <code>: <detail>`; successful text replies use `OK`. JSON mode emits one JSON response per request line. Transport framing and field names are part of this adapter's API; executable table addresses are not.
