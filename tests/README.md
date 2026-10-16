# Tests without game data

Build all targets and run CTest normally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBMH_DATA=
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Synthetic tests exercise generated file formats, fonts and palettes, JSON, command parsing, archives, audio, and engine/UI rules. Their bytes and messages are authored by the tests. Data-dependent tests remain listed, but report **Skipped** with return code 77 when their external inputs are not configured. A skipped test is not evidence that the corresponding game feature passed.

`BMH_DATA` has no default installation path. A nonexistent directory or a directory without a game executable is a configuration error; the tests validate the supplied content. `BMH_ENGLISH_DATA` optionally selects a separately prepared English installation for its data tests.

Some integration tests also need externally supplied savegames or reference expectations. None are distributed here. Their individual CMake cache options use names such as `BMH_TEST_<TEST_NAME>_INPUT_1`; inspect them with `cmake -LAH build`. `BMH_UI_REFERENCE_SAVE`, `BMH_UI_PREPARATION_SAVE`, `BMH_UI_SETTINGS_SAVE`, `BMH_STATISTICS_FINANCE_TEST_SAVE`, and `BMH_REFERENCE` are explicit optional references. Missing required references skip the affected test, without disabling unrelated synthetic tests.

The `BMH_TEST_INPUTS` directory supplies helper inputs with neutral names:

- `bank-deadline.fbm`, `bank-timer.fbm`, `bank-cup.fbm`
- `loan-return.fbm`, `human-transfer.fbm`, `national-pending.fbm`, `stadium.fbm`
- `format-start.fbm`, `format-players.tsv`, `format-matches.tsv`, `format-team-values.tsv`
- `tax-animation-script.bin`, `camp-label-colors.bin`
- `expected-text.tsv`

`expected-text.tsv` contains one `key<TAB>expected text` record per line, using the keys requested in the test source. `format-players.tsv` holds whitespace-separated player id, condition, technique, form, age, position and strength. The match table uses home name, away name, home goals and away goals separated by tabs. The team table holds team id, condition, technique, form, morale and strength. These reference tables must match the external installation and saves; do not substitute empty files to obtain a pass. Reference values, game text and binary excerpts belong outside this repository.

`BMH_TEST_CLI_TRANSCRIPTS` selects external command transcripts for the full console integration suite. The independent `cli_parser_unit_tests` still runs without that directory or game data.

When adding a test, generate its fixture from a small documented pattern whenever possible. Keep installed-game checks separate and register required inputs through `bmh_add_data_test`. A directly invoked data test must also use return code 77 for an unavailable optional input, rather than printing “skipped” and returning success.
