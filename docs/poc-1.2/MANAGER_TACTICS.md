# Typed human tactical fields

World schema version 6 adds optional `Manager::tactics`. Versions 1–5 remain readable and leave it explicitly uninitialized. The manager-free imported master needs no changes. Snapshot framing remains `OBMH12S4`; tactical values are part of the embedded versioned world JSON.

## Verified fields

Each of the 24 tactical rows retains its four ORDER instruction bytes (Original row offsets +2..+5), row-partner selector (+0x3f), depth (+0x41) and lane (+0x42). Manager automatic weights (+0x288) and the automatic formation selector (`manager_options[seat*5]`, 0 = manual, 1–8 = selected formation) also persist. Row-partner references are bounded row indices or 255, not player identities. Starting positions use the verified 0–19 depth / 0–8 lane limits. Reserve-row coordinates and raw instruction bytes are retained without inventing normalized defaults; actual edits use the Original clamp.

No pointer-bearing tactical records, local packed player IDs or unverified board bytes are copied into the world. The scalar extractor `manager_tactics_from_legacy` reads only these known fields from an explicitly supplied initialized legacy manager. It does not establish that manager's ownership in a typed world and is not a save/career converter.

## Supported actions

`calculate_manager_tactical_action` creates a bounded temporary workspace for a single human club. Stable 32-bit player IDs are checked against the observed roster row and mapped to local IDs only inside that workspace. Only known tactical fields are read back. The following existing Core functions perform the actual changes:

- `set_player_order_source`: four individual instructions, Original clamp 2–40; a non-starter cannot be edited as a starter.
- `move_pitch_player_source`: placement and occupied-position exchanges, outdoor adjacency rejection, indoor partner linking/clearing, injury checks, and switching successful moves to manual mode.
- `set_pitch_automatic_weight_source`: Original two-bit cycling with mode and expected-old/new-value checks.

The buffer capacity required by those helpers is allocated; no formation values are invented or applied. Custom formations, formation optimization/application, starting-lineup roster exchanges, designated set pieces, other tactical board fields and human match simulation require their own adapters. Indoor placement support does not claim complete indoor match or lineup support.

`TacticalPolicy` (mode, indoor and frozen state) is authoritative session policy, not a frontend-controlled way to bypass rules. Frozen state rejects all editing. A missing initialized tactical state is an explicit error.

## Ownership and publication

`apply_manager_tactical_action` reuses the session's `ManagerActionOwner`: sparse manager seat, stable club, nonzero scope and revision must agree. The observed player and instruction or position/weight values must still match. Only changed tactical fields increment the owner revision. Rejections and accepted clamped no-ops preserve world and revision. Tactical assignment is non-throwing; unrelated finances, roster, player attributes/cards and other managers remain unchanged.

These rows follow the manager's current roster row. A future roster-swap/transfer owner must update player-following tactical data and slot-following coordinates using the Original swap semantics; it cannot simply reorder the typed roster and leave a stale board. The existing AI projection includes tactics in manager-ownership checks and still rejects human participants. This packet does not enable human clubs as AI matches or connect the legacy GUI to the typed career.

## Checks

Synthetic tests compare supported actions directly with the Original helpers in a separately mapped native workspace (manager seat 3, club 72, reversed local player IDs), while the typed world uses global IDs above 65,535. Tests exercise German and English ownership, all four instructions, clamping, starters versus bench, outdoor/indoor occupied and adjacent targets, injury, mode-dependent weight cycling, stale scope/club/player/values, replay, frozen edits, no-ops and persistence. Older version-5 manager states retain finances but have no guessed tactics.

An optional private test reads a supplied Original save, extracts its known tactical scalar fields, checks their typed roundtrip and compares twelve ORDER edits directly against the source save's manager. The source save remains read-only; no complete legacy career conversion is claimed.
