# Country-owned domestic cup stages

The 1.2 adapters now cover domestic entrant selection, original draw algorithms and atomic score/stage progression. They are not yet the application's complete cup simulation path: match simulation, human-manager effects and other competition/economy ownership still need integration.

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

Domestic draw presentation requires at least one human manager with a current league club in that country's ladder. A manager in a lower division qualifies, even if their club is no longer in the cup field. Germany-only careers receive no English domestic draw notices, England-only careers receive no German ones, and mixed-country careers receive both. `cup_draw_notice` applies the same rule to initial draws; `commit_day` returns filtered notices for subsequent round/stage draws. Return legs and completed finals do not emit a new draw. Source display settings can further suppress presentation in the future application presenter.

This is a presentation filter only. AI-country result processing, stage links, shared dates and RNG states remain identical with or without the audience. Synthetic tests compare both paths directly.

## Results, draws and transaction boundaries

Each round keeps its first-leg fixture orientation and the original score/phase words. The existing `bmh::cup_tie` calculation determines qualifiers, including aggregate and away-goal handling. Return-leg fixtures reverse the original pairing. Unresolved ties are rejected rather than assigned an arbitrary winner.

The low-level stage transition requires a complete `next_draw` after a non-terminal round, containing exactly the preceding winners. After a linked stage ends, `next_stage` must match the source link and include every qualifier and seeded club. This stage validator does not consume RNG.

The application owner in `src/app/world12_cups.*` now supplies those receipts automatically. It stages the career and random stream, invokes the shared original draw kernels, validates all country results/links, then publishes both state and RNG seed together. Failure in England also rolls back Germany's already prepared draw. The day API expects the match owner's staged post-match snapshot; it does not simulate scores or account for missing match RNG draws.

Initial fields use explicit rank-ordered lists for all five leagues in each country, validated against the world's exact memberships. Germany selects 18 + 18 + 14 + 14 clubs. England selects 20 + 20 + 20 + 8 for the FA Cup and 20 + 20 + 4 for the League Cup. Each English Premier League seed prefix is shuffled separately from its qualifying field; its stored order is reversed to match the Original's backwards read of the reserved prefix. Membership order must not be substituted for a previous-season ranking by a running career.

`game/cup_draw_rules.hpp` is shared by the legacy and typed paths: initial fields use one GameRandom draw per slot; later shuffles use two Watcom draws per entrant. Later pairs join the first and last remaining elements. The original lower-division home priority, league weights and exchange-sort tie ordering are retained. The original entry-0 restriction on later national home ordering also remains; English proper-cup/League-Cup successor stages are not silently treated as German entry 0. First legs and terminal finals make no next-field draws. The source's terminal-stage return is preserved.

Initial semantic tie ordering runs independently of whether a human sees the draw. Hiding an AI-country presentation is not the legacy source's display-off early return. Display visibility therefore cannot change participant selection, home order or RNG. Remaining legacy overview/manager bookkeeping and financial/title effects still need their typed owners; they are not implemented by displaying a notice.

`prepare_day` identifies each pending domestic cup leg alongside the day's league fixtures. `commit_day` validates the complete country/league/cup batch on a staged career, then publishes results, stage transitions and the shared date together. Missing results, missing successors, invalid draws and stale/replayed tickets preserve the previous career. Unsupported non-league events continue to block advancement explicitly.

The development O1.2 snapshot schema `OBMH12S3` retains stage links, seeded participants, all completed stage/round results, pending return legs and revisions. Read validation checks the whole stage chain. This is an unpublished development schema; it is not a migration path for legacy/1.1 saves or a complete application save implementation.

## Evidence and remaining integration

Synthetic tests cover mixed German/English cup days, atomic rollback with concurrent league results, stale tickets, aggregate/away-goal resolution, seeded qualification, linked stages and truncated saves. A complete test-score programme accounts for 63 German-cup, 67 FA-Cup and 73 League-Cup matches, with save/reload after every calendar day. The automatic-draw owner additionally verifies 176 initial GameRandom draws and 420 later Watcom draws over that programme. Reversed ranking inputs prove qualification follows rank rather than numerical club IDs. These 203 matches use supplied test outcomes, not the match engine.

`world12_cups_validate` checks all eight supported stage descriptors against locally supplied German/FLTD source rules and a typed world. It also creates the three initial fields from supplied world membership order and checks their typed save roundtrip. Its descriptor pairings are synthetic metadata probes; its membership order is not an independently verified previous-season table. It does not validate a played career. Private input/output files stay outside the public repository.

Still required: match simulation/after-match integration, supercup participant selection, European/group/indoor/national competition adapters, source-compatible premiums/title/manager records, complete application progression and native multiplayer QA.
