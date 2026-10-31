# 1.2 match integration

The typed world now invokes the existing ported `MatchEngine` for actual AI league scoring phases. It no longer needs a supplied result for this bounded operation. This is not yet a complete matchday or playable career adapter.

## Identity and country context

`AiMatchProjection` maps only the requested clubs/players into a bounded legacy workspace. Global 32-bit identities remain outside all packed local roster words. Unrelated human managers may exist in the world, but a fixture containing a human-managed club is rejected until its tactical/financial owner is implemented.

Local club numbers cannot stand in for league/country identities. Runtime-only `GameState::projected_match_clubs` supplies each club's league class, climate/country code and foreign-player limit. Domestic league classes follow the country's ladder index; European classes use the existing foreign-league slots. Country codes remain Germany 0, Spain 1, England 2, France 3, Italy 4 and Portugal 5. English clubs therefore use their English nationality when the original foreign-player rule is enabled, while retaining the domestic cap rather than the old foreign-club cap. Crowd/climate calculation and automatic lineup consume this explicit context.

Empty context retains the original positional lookup for legacy careers. A projected local workspace cannot be exported with the legacy `save_stream`; its local IDs are not a career save format.

## Scoring boundary

`simulate_ai_league_match` requires a same-league AI fixture, supplied formation tables, explicit weather/month/date/attendance, referee properties and presentation-button settings. It reuses Original `prepare_team`, `auto_lineup`, `build_side_records` and the existing `play_matchday` phase driver. Goal decisions, AI substitutions, cards and injuries come from that engine, not from generated result data.

The function stages both random streams and the projected match state. It publishes player attributes/card words, roster order/flags, club formation/match flags and low morale bits only after the simulation and full world validation succeed. Global player membership cannot move between clubs through this boundary. Stale player/club/manager/RNG ownership, malformed formations and unsupported short-squad forfeits reject without publishing match effects or RNG. An external change detected at late commit remains intact; it is not overwritten by a stale match result.

World schema version 3 stores player `cards` and club `match_flags`/`morale`. Version-1/2 imported worlds remain readable with initial defaults. The existing development O1.2 snapshot embeds these fields through the world codec. A probe's post-scoring snapshot verifies that codec; it is not a resumable complete in-match career save.

## Evidence and limits

- Sixteen deterministic synthetic-fixture runs (and their repeats) invoke actual scoring phases across German, English, Spanish and Portuguese leagues. They produce goals and reach 90–94 minutes.
- Explicit-context side ratings/RNG are compared directly with equivalent original positional lookups, without duplicating the rating formula. An enabled-foreign-rule test checks English reserves are treated as domestic players.
- Germany-only and England-only worlds reproduce their retained fixtures' scoring, RNG and club/player match mutations exactly as in the combined world. Removing the unrelated country does not change local match classification.
- Projection tests cover wide identities, roster/card/formation/morale persistence, stale ownership, illegal cross-club membership, malformed attributes and the legacy-save export guard. A debug-hook interleaving forces a late stale commit after real simulation and verifies neutral RNG/publication.
- `world12_ai_validate` runs seven fixtures from the real typed master using supplied reference state/rules/referee data, then checks post-scoring world-state snapshots. Weather/attendance are explicit probe inputs rather than a complete generated matchday.
- The older `week199_capture` diagnostic does **not** pass full Original side-record equivalence (1/88 full records). A separate clean `ada105f` build produces byte-identical diagnostic output to this change, so that diagnostic exposes no newly introduced difference. It is not reported as a passing Original oracle.

Still required: the full matchday owner, generated weather/attendance/referee selection, after-match development/morale/economy/statistics, forfeit settlement, human tactics/live interventions, cup/national match setup, match-event translation and the typed GUI-career adapter. No league day or career date is advanced by this scoring API.
