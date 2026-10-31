# Country-owned domestic cup stages

This is the 1.2 score/progression adapter, not yet the application's complete cup simulation path. Initial qualification, the actual original draws, human-manager effects and match simulation still need their owning application adapters.

## Source rules and ownership

`KnockoutStage::from_source` reads the original stage's flags, number of entrants/rounds, linked successor and count of seeded clubs. It verifies the source country and original competition identity. Entrants and seeded clubs use global 32-bit club IDs. A country's domestic competition cannot contain a foreign club or a national team.

The supplied sources were checked against these stages:

| Competition | Source stages | Structure |
| --- | --- | --- |
| German cup | 0 | 64 entrants, six rounds |
| German supercup | 23 | Two entrants, one round |
| FA Cup | 0 → 28 | 48 qualifying entrants, two rounds; 12 qualifiers plus 20 seeded clubs enter the 32-club proper cup |
| League Cup | 25 → 26 → 27 | 24 qualifying entrants; 12 qualifiers plus 20 seeds; four two-legged rounds from 32 clubs; separate single-match final |
| English supercup | 23 | Two entrants, one round |

The source entry number is local to its country and competition. German entry 0 and English entry 0 never share mutable counters or results.

Domestic calendar masks route independently: `0x20` is the country's national cup and `0x2000` its supercup. English `0x4002` is the League Cup; German `0x4002` remains an indoor event. Indoor, European and national-team appointments cannot be satisfied by a domestic cup receipt.

## Results, draws and transaction boundaries

Each round keeps its first-leg fixture orientation and the original score/phase words. The existing `bmh::cup_tie` calculation determines qualifiers, including aggregate and away-goal handling. Return-leg fixtures reverse the original pairing. Unresolved ties are rejected rather than assigned an arbitrary winner.

After a non-terminal round, the draw owner must supply `next_draw`. It must contain exactly the preceding winners. After a linked stage ends, `next_stage` must match the source link and include every qualifier and seeded club. This adapter neither invents draw order nor consumes RNG in place of the original selection/draw routines. Home ordering and original draw-related simulation effects remain the draw owner's responsibility.

`prepare_day` identifies each pending domestic cup leg alongside the day's league fixtures. `commit_day` validates the complete country/league/cup batch on a staged career, then publishes results, stage transitions and the shared date together. Missing results, missing successors, invalid draws and stale/replayed tickets preserve the previous career. Unsupported non-league events continue to block advancement explicitly.

The development O1.2 snapshot schema `OBMH12S3` retains stage links, seeded participants, all completed stage/round results, pending return legs and revisions. Read validation checks the whole stage chain. This is an unpublished development schema; it is not a migration path for legacy/1.1 saves or a complete application save implementation.

## Evidence and remaining integration

Synthetic tests cover mixed German/English cup days, atomic rollback with concurrent league results, stale tickets, aggregate/away-goal resolution, seeded qualification, linked stages and truncated saves. A complete test-score programme accounts for 63 German-cup, 67 FA-Cup and 73 League-Cup matches, with save/reload after every calendar day. These 203 matches use supplied test outcomes, not the match engine.

`world12_cups_validate` checks all eight supported stage descriptors against locally supplied German/FLTD source rules and a typed world. Its pairings are explicitly synthetic metadata probes; it does not validate original entrant selection or a played career. Private input/output files stay outside the public repository.

Still required: original field-selection/draw ownership with transactional RNG, European/group/indoor/national competition adapters, source-compatible premiums/title/manager records, complete application progression and native multiplayer QA.
