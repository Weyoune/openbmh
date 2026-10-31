# International-league POC

Working branch: `poc/1.1-international`, based on `v1.0.2`. Stable main remains separate.

Implemented:

- Explicit `OBMHPROF.INI` metadata; unmarked installations retain Classic dimensions.
- Five German divisions unchanged; international sizes 18/20/18/18, 164 league clubs.
- Twenty starting players and 24 slots per league club; 4,158 runtime roster/auxiliary slots.
- Profile-aware master-database and executable-data views, preserving executable bytes on disk.
- Count-derived club records, opponent histories, kit/short-name and fair-play tables.
- Per-league fixtures and 38 foreign calendar dates, with smaller leagues stopping at 34 rounds.
- Extended save identity `O1.1`; Classic retains `V2.24` and its existing dimensions.
- Offline private data preparation, provenance, crest fitting and Original-format VGA output.

Local integration evidence so far: the expanded career completed 180 turns with seed 1 (three season ends), and another 180 with seed 11 (two season ends). No simulator invariant violations or contract-policy errors were reported. These are automated manager-policy runs, not claims of complete human gameplay QA or validation of all editions/platforms.

The prepared private dataset has 445 club/country entries, 3,334 players and 263 individual foreign club crests. Original German rosters and all 2,574 Original player word records are preserved by assertions during preparation. All 263 foreign crests have supplied assets; no fictional crest fallback was needed. Donor fields retain their real/fictional provenance. No original game content is committed here.

Remaining release gates: complete configured regression tests, explicit save/reload and cross-profile rejection checks, screenshots of the expanded tables/crests, and platform package validation. See [FORMAT.md](FORMAT.md) for dimensions, preparation and distribution boundaries. A development commit is not a completed release claim.
