# Legacy installation importer (first version)

`openbmh_import` converts a locally supplied German BMH installation or English
Football Limited installation into an OBW ZIP, or an unpacked OBW directory.
It is a separate tool: neither game frontend nor the engine reads OBW yet.
Integration into the game belongs to version 0.2.

## Build and use

Configure the project as described in [BUILDING](../../BUILDING.md), then build
the additional target:

```sh
cmake --build build --config Release --target openbmh_import
./build/openbmh_import --data "path/to/game" --output "season.obw" \
    --start-date 2026-07-01 --title "My imported season"
```

With a Visual Studio multi-configuration build, use
`build\Release\openbmh_import.exe`. On Linux and macOS, the executable is in
`build/openbmh_import`. To inspect or edit the JSON directly:

```sh
./build/openbmh_import --data "path/to/FLTD.EXE" --output "season-directory" \
    --start-date 1995-07-01 --directory
```

`--data` accepts an installation directory, a parent containing exactly one
installation, or its `BMH.EXE`/`FLTD.EXE`. File discovery ignores case. An
installation containing both executables requires naming the executable.
Only the executable, master database and text database are required; absent
graphics, audio or desktop files do not prevent conversion. Source ZIP/7z
archives and individual `.FBM` saves are not input formats for this first tool.

The reference date is explicit because a master database records ages without
an unambiguous season year. It must be July 1, from 1900 through 9998. The tool
does not use the computer's current date. It refuses existing output paths,
including symlinks. Output is assembled in a sibling staging directory and
published after validation; the output parent must already exist.
Do not run competing writers against the same destination.

Runtime packages for 0.1 retain their existing products. The source build has
the extra importer; `cmake --install build --component ImportTools` installs it
separately. Installing it does not enable OBW in the game.

## Conversion profile and coverage

The manifest declares format `1.0`, `kind: world`, provenance `import:bmh-de`
or `import:fltd`, and `content_license: local-import-only`. Crucially,
`requires: ["import:legacy-database-v1"]` and `x-import.playable: false`
identify an **intermediate conversion**, not a fully initialized playable world.
A reader which does not support this profile must refuse it. Future conversion
passes can complete its mappings without needing to reopen the original database.

Every core table uses the `schema`/`items` envelope, including `rules` and
`calendar`. The schemas under [schema/1.0](schema/1.0) describe this first
conversion profile; complete save/pack/theme schemas and engine playability
validation remain future work.

| Source | Exported data |
|---|---|
| `M3D.MAN` / `M3E.MAN` | Named clubs, national teams, nations, all 2,574 players, master squad assignments, stadium name pool |
| Player words and bonus bytes | Position, age, nationality including the high bonus bit, form/technique/condition, seven skills, bonus and derived display strength |
| Executable, through existing loaders | Nine league sizes/tiers/participants, original fixture pairings, promotion-ladder inputs, 366 source calendar words, training-camp prices/quality and credit factors |
| `TEXTD.BLK` / `TEXTE.BLK` | Local `de-original` / `en-original` gettext layer and an indexed text table with raw bytes |
| Unmapped master fields | Named hexadecimal tables, original name pools, original player words, badge/kit selectors, squad words and flags |
| Source master database | Exact file bytes in `x-legacy/source-database.json`, hexadecimal, for lossless later re-mapping |

`x-legacy/import-report.json` records warnings and outstanding mappings. Staff,
contracts and finance tables are empty: their initialized values are created
by career setup, rather than supplied as complete records by the master database.
Stadium names are exported without invented owners or capacities. Squad rows are
preserved as legacy rows, not claimed to be shirt numbers. A source player name
is kept whole in `name.last`; the importer does not guess first/last-name splits.

Exact birth dates are unknown. A player instead carries
`age_at_start: {"years": 22, "on": "2026-07-01"}`. This is an explicit alternative
to `born`, not an assumed birthday. Attribute scales are the stored scales:
form, technique, condition and bonus 0–127; skills 0–3; display strength 0–99.
`bmh-classic@1` in `world/rules.json` declares those scales and marks strength as
derived. These are master seeds, before career initialization varies ratings,
rewrites nationality fields or shuffles league participants. No career is
started and no random stream is consumed during import.

### Identifiers and edition mappings

Ids derive from source identity slots, not names: `club:00000`, `player:00000`,
`stadium:00000`. National teams use `nt:<nation key>`. Recognized geographic
aliases get ISO nation keys (England/Scotland/Wales/Northern Ireland use private
football-association keys). Unrecognized or duplicate labels get
`nation:x-legacy-<index>` with confederation `other` and a warning. Foreign clubs
whose nation has not been decoded refer to `nation:x-legacy-unassigned`.
The source index always remains in `x-legacy`.

| Source leagues | German | Football Limited |
|---|---|---|
| 0, 1 | Tiers 1, 2; 18 clubs each | Tiers 1, 2; 20 clubs each |
| 2, 3 | Parallel tier 3, ids ending `-3-a` / `-3-b`; 18 each | Tiers 3, 4; 20 each |
| 4 | Tier 4; 18 clubs | Tier 5; 10 clubs, `played: false`, no fixture generator |
| 5–8 | Four 12-club leagues, using nation slots 1–4 | Same, with the English database's nation-slot order |
| 138–167 | Named amateur-pool slots | Only named slots become entities; unnamed slots remain in the source data |
| 168–356 | Foreign teams without squads | Same |
| 357–418 | National teams / nationality labels | Same |

Foreign strength bytes encode a band and style flags, not a 0–255 rating. They
stay in `x-legacy.strength_code` until a typed interpretation is implemented.
Fixture tables store home/away **draw-position pairs**, not final shuffled
career fixtures. Calendar words remain source day offsets; competition options
and weekday alignment have not run. Promotion and European-place bytes are
retained as inputs, without pretending that cup qualifications have been mapped.

The existing executable loaders apply OpenBMH's supported rule profile, and the
English loader translates FLTD data into the common address space. The manifest
records `rule_profile: openbmh-supported`; this tool does **not** claim an
unmodified dump of every original edition's rules. No executable is included in
the OBW file.

### Text and rights

Text follows the existing ISO 646-DE substitutions. Extended bytes are preserved
as Latin-1 Unicode code points and original hexadecimal bytes; control/layout
codes remain available in the legacy text table. The local gettext catalogue
uses stable `legacy.text.<index>` message ids with `legacy-text` context. It is
a preservation layer, not a completed translation of the modern interface.

Imported names and texts retain their original ownership. Keep the resulting
world on your machine; the importer does not grant permission to distribute it.
The public repository contains only implementation, schemas and synthetic tests.

## Validation and remaining work

The tool checks required documents, table versions, duplicate identities,
cross-table team/nation references, stage participant counts, fixture rows,
source squad ownership and supported edition sizes before publishing output.
Malformed/truncated input is an error; it does not substitute synthetic game
content. `obw_import_tests` covers synthetic German/English mappings, legacy
reader input, archive/directory round trips and rejected inputs.

Still to implement: full cup definitions/qualification and dated calendars,
complete rule mappings, initialized finances/contracts/staff/stadiums, asset
conversion, `.FBM` to `.obs`, a general OBW reader/validator and the 0.2 engine
adapter. First-season equivalence and save continuation regressions only become
possible when that adapter exists; the first converter does not claim them.
