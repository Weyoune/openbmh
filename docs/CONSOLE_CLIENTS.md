# Writing a console client or agent

The console is a local control interface for one running career. Human clients, bots and alternative frontends use the same actions and rule checks. Start with [the console reference](CLI_CONCEPT/README.md); use runtime `ListOptions` and `Help` rather than guessing a command from a graphical label.

## Choose a transport

For a single process, start `openbmh_cli --console --mod "mods/BMH-classic/content.obw" --saves "path/to/saves"`. Send command lines on stdin and finish each block with `go.`. `--json` provides structured output records.

For separate manager processes, start a local mailbox server:

```sh
openbmh_cli --console --mod "mods/BMH-classic/content.obw" --saves "path/to/saves" --serve mailbox --seats 4 --seed 1
```

A client sends one block on stdin to:

```sh
openbmh_cli --send mailbox/seat1 --timeout 3600
```

The client still supplies a terminating `go.` line. Seats are numbered 1–4 and represent human manager slots, not runtime team IDs. Submit one request at a time for each seat. A long-running `WaitTurn` or match-clock request must finish before another client uses that seat. A timeout or interrupted shell client does not establish that the server cancelled the game action.

Alternatively, `--listen PORT --seats N` binds a local socket on `127.0.0.1`. A connection selects `SEAT k` when seats are enabled. Blocks end with `go.` and responses with a line containing only `.`. Keep one connection/request owner per seat. See `--help` for supported combinations of transport and log options.

## Track ownership and the whole response

Keep the current seat, window, manager/club identity, phase and pending question/editor. Parse the entire response before choosing the next action. A question can arrive in information produced during another manager's turn or a suspended calendar advance. Match the question's recipient to this client; answer with its actual ID and listed option.

The seat hub can prefix a response with information accumulated while other managers played. Do not mistake an old desktop in that feed for the current owned projection. Retain questions and errors from the full response, then use the current own state for financial or squad decisions. A new window header is not emitted after every operation. Status can instead report `matchday running, minute N`.

Use `WaitTurn` when another seat owns management. At a desktop, apply a short batch of independent decisions, then put `Kickoff` last. Reinspect after actions that open a question, negotiate a contract or change the meaning of a row. Unknown questions require a policy decision; `GoBack` can commit a default or cancel an editor and must not be used as a generic acknowledgement.

## Shared matches and calendar boundaries

In a stepped match, `Run` and `Step` participate in a barrier across the required seats. Put the clock command last. Read the returned actual minute and phase before choosing another stop; do not request half-time after that boundary. A manager without a fixture still participates in the barrier but should not issue substitutions or tactical edits for another team's match.

Questions and staged offer editors can suspend a calendar operation. Finish the owning question/editor before requesting another advance. Once an answered clock request returns without advancement, inspect why before sending another readiness request; blindly replaying a mutation can duplicate an effect.

A useful automated policy inspects budget, advertising, contracts, squad availability and tactics at deliberate intervals, using explicit rules and bounds. `Advice` supplies observations, not a complete strategy. Count actual `season_index` changes and inspect `phase` at year end; a historical table or summary does not constitute another completed season.

## Reproducibility and recovery

Record the binary version, data edition, seed, time mode and the exact submitted blocks when a reproducible session matters. The seed alone is insufficient if the initial save, data or command order differs. `--log`, `--replay` and, for supported multi-seat sessions, `--resume` provide execution records; a replay must use compatible inputs.

Keep an independent input receipt. Mailbox request files are transport state: a held request may be rewritten to `WaitTurn`, including a request originally submitted as a clock command. Such a file is not proof of what was first sent. Preserve responses and raw bytes before decoding diagnostics. Do not publish logs containing proprietary game text, saves or personal data.

For restart, use a fresh mailbox, pin the intended binary and verify the recovered current phase, manager and pending operation. Do not silently adopt an old client request or run two processes against the same seat. Normal saved games and replay are the supported starting points; editing native state to get past a question defeats the ownership checks the client is meant to exercise.
