# Alternative frontends

A frontend can use the console without duplicating gameplay rules. Start `openbmh_cli --console` with standard input/output, a directory mailbox or a loopback socket. All three reach the same session. See the [client introduction](../CONSOLE_CLIENTS.md) for framing and ownership.

`--json` emits one JSON object per output line. Each has a line class, window, player and text; structured information is carried in `data`. Table records include both displayed cells and named values where available. Question records include their ID, recipient, options and default. Inspect these fields instead of parsing translated labels.

`State(view)` returns an owned projection. Supported views include `Desk`, `Table`, `Lineup`, `Mailbox`, `Bank`, `Market`, `Advertising`, `Contracts`, `Stadium`, `Toto`, `Training` and `Camp`. Mutations still use the current window's commands and the session's normal validation. A state snapshot does not grant permission to act for another manager.

Semantic fields such as `plain_background`, `solid_panel_background`, `compact_dialog_frame`, `replay_progression_mode` and `replay_chance_mode` describe projected behaviour. Their names are independent of executable table locations. Presence flags such as `has_replay_progression_mode` describe whether the corresponding value is available; they are not extra gameplay switches.

Rendering should retain the revision and identifiers associated with a projection. Refresh after successful mutation, manager handoff or a season transition. Modal editors have their own lifetime: update their staged fields or cancel through their owning action, and do not keep rendering pointers into a mutable session object.

The transport is a local integration interface, not an authenticated public game service. A network-facing application needs its own access control and deployment design; changing the socket bind address is not a complete service architecture.
