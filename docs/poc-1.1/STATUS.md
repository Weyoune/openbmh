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

The prepared private dataset has 445 club/country entries, 3,334 players and 263 individual foreign club crests. Original German rosters and all 2,574 Original player word records are preserved by assertions during preparation. The visual review identified 175 usable supplied foreign crests and 88 missing/shared Original placeholders. Those 88 now have explicit fictional club-specific monograms instead of the computer symbol; the updated crest preparation uses the bright Original background texture and preserves black logo details. Donor fields retain their real/fictional provenance. No original game content is committed here.

Validation: 37 configured synthetic tests executed successfully, with 158 external-input tests explicitly skipped; four offline preparation tests passed. Extended save/reload and both cross-profile rejection paths passed. Two separate season-end snapshots each contain every fixture in all nine leagues. A Classic 80-turn run produces a byte-identical save to stable 1.0.2. Screenshots and saves are archived privately. Native platform CI also builds and tests Windows x64, macOS ARM64 and Linux x64; release QA receipts identify the exact successful revision. See [FORMAT.md](FORMAT.md) for dimensions, preparation and distribution boundaries. A development commit is not a completed release claim.

The next community package uses a data-only `COMMUNITY.OBD` bundle and no DOS executables. See [STANDALONE.md](STANDALONE.md). The Windows community ZIP omits source archives and loose PNG/JPEG studies; corresponding sources remain available separately.
