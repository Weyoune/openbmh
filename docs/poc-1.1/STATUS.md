# International-league POC

Working branch: poc/1.1-international, based on v1.0.2. The stable main line is separate.

The explicit `OBMHPROF.INI` profile marker selects classic-plus-1; missing metadata retains the original Classic mode. Unknown or inconsistent metadata is rejected. Profiles define database counts and roster boundaries, with tests for the existing layout, the expanded layout and packed-player limits.

Target: five German divisions unchanged, international sizes 18/20/18/18, 164 league clubs, 24 roster slots and normally 20 starting players per league club. The expanded runtime needs 4,158 roster and auxiliary slots. Player IDs remain below the 4,095 empty sentinel; absolute slot references must not be truncated to twelve bits.

This is a development status, not a claim of completed expanded gameplay. Next stages migrate semantic limits and index-dependent buffers, prepare the private dataset, distinguish extended save identity, and verify two complete seasons. No original data or rendered proprietary assets belong in this public tree. Classic readers retain their default counts and byte structure.
