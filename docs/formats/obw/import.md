# BMH and Football Limited installation converter

`openbmh_import` imports the content of a supported, locally supplied installation
as **BMH-classic** or **FLTD-classic**. Football Limited's executable is named
`FLTD.EXE`; the spellings `FTDL-classic` and `FTLD-classic` are accepted as CLI
aliases and written as `FLTD-classic`.

The output is an `.obw` ZIP, or an equivalent directory. It contains the complete
**Classic installation content**, including graphics, portrait parts, fonts,
audio and the original data tables. It contains no running career or executable.
The existing 0.1 game does not read OBW. The 0.2 development branch implements the
versioned [Classic contract](classic-v1.md) through its
[native runtime and launcher](runtime-0.2.md). This remains separate from
migrating every engine rule to the future generic model.

## Build and use

Configure as described in [BUILDING](../../BUILDING.md), then:

```sh
cmake --build build --config Release --target openbmh_import
./build/openbmh_import --data "path/to/BMH" --output "BMH-classic.obw" \
    --start-date 2026-07-01 --mod BMH-classic
./build/openbmh_import --data "path/to/FLTD.EXE" --output "FLTD-classic.obw" \
    --start-date 1995-07-01 --mod FLTD-classic
./build/openbmh_import --validate "BMH-classic.obw"
```

On a Visual Studio multi-configuration build use
`build\Release\openbmh_import.exe`. `--mod` is optional: the tool detects the
edition. A mismatching explicit mod is an error. `--title TEXT` changes the
world's display title. `--directory` writes an unpacked world, which `--validate`
can read too. The source may be an installation directory, its executable, or a
parent with exactly one installation. File discovery ignores case. If both game
executables exist, select the intended executable explicitly.

A full import requires the same installation resources as the current launcher:
master database, text database, executable, `M3.MEN`, `M3.DAT`, palette, required
`KB`/`W` pictures, two fonts and four sound banks. A missing resource is an error.
Supported inputs are the German CD-family editions and Football Limited; the
1994 floppy `M3.EXE`/`M3PC.MAN` family and Amiga installations are not supported.
Source ZIP/7z and `.FBM` files are not input formats for this tool.

The date must be July 1 of an explicit year (1900–9998). Master data contain
ages without an unambiguous season year; the converter never uses today's date
or invents birth dates. Player age is recorded as `age_at_start`. Calendar setup
remains a Classic runtime operation using the selected season/options.

Existing destinations, including symlinks, are refused. The parent directory
must exist. The tool stages and validates output before publishing by rename;
source files are read only. Do not run competing writers for one destination.

## What a Classic world contains

The manifest names `mod: BMH-classic` or `FLTD-classic`, profile
`classic-installation-v1`, `requires: ["runtime:classic-v1"]` and
`content_license: local-import-only`. `content_complete: true` means all inputs
for that **specific Classic runtime** are present. It does not claim that the
0.1 client can load OBW or that every rule has already been migrated to a generic
0.2/0.4 schema. A reader without `runtime:classic-v1` must refuse the world.

| Area | Contents |
|---|---|
| Nations and teams | All named clubs/national teams, nine leagues, amateur pool, foreign nation blocks, encoded strength band/style, crest and kit selectors |
| Players | All 2,574 identities, squads, raw words/flags, attributes, age reference, nationality and frozen portrait recipe |
| Staff | Referee names, properties and strictness; human/computer manager setup is a runtime initializer |
| Contracts and finance | Contract seeds per assigned player and finance policies for all 138 league clubs; actual values are created by the same manager/season initialization as today |
| Stadiums | Entire name pool and stadium initializer, with all building, capacity, price and facility tables retained in Classic data regions |
| Competitions | Nine league definitions and fixture generators, plus every configured cup-table stage with links, flags, group/qualifying information and runtime participant resolvers |
| Calendar and rules | Day table, league bindings, cup series, ladder, training camps, credit factors, initialization sequence and all remaining data-region content |
| Text | UTF-8 original gettext layer, indexed strings, raw text blob and embedded executable-data texts |
| Artwork | Every numeric `KB/*.VGA` and `W/*.VGA` decoded as indexed PNG plus exact pixel indices; crest crops, original palette, portrait parts, bitmap fonts |
| Audio and optional art | Four original audio containers with chunk descriptors; optional FLI/FLC animation and WAPPEN.LBM when present |
| Preservation | Exact master-database bytes, named auxiliary tables/pools, original and effective data regions, resource metadata and CRC32 inventory |

A source database does **not** contain ready-made human-manager finances,
contracts or stadium capacities. Those depend on the selected club, game level,
mode and random seeds. The world specifies their initialization rather than
inventing a balance, contract term or stadium owner. Imported player attributes
are the master seeds; the existing Classic initialization changes ratings and
ages and can shuffle club draw positions. This consumes no RNG during import.

Player `appearance` is computed from the **raw legacy byte name**, before Unicode
conversion. It freezes the background and five layer indices once. Name overlays
must retain this field. The imported set stores individual transparent parts and
placement metadata; composed player faces are never stored. Packed atlas parts
which cross a source row boundary retain their original pixels.

The effective data use OpenBMH's existing supported rule profile. Unpatched
source data are preserved separately, including Football Limited's native data
segment. Neither copy includes a DOS header, executable code or relocation
instructions. `x-classic` is a documented compatibility bridge, not a claim that
the current address-based engine has already been replaced.

## Stable identities and original encodings

IDs derive from master identity slots: `club:00000`, `player:00000`,
`stadium:00000`; national teams use `nt:<nation-key>`. Recognized countries get
ISO keys, with private association keys for England/Scotland/Wales/Northern
Ireland. Unrecognized/duplicate country labels retain `nation:x-legacy-<index>`
and a report warning, preserving modded or historical identities.

Foreign club indices 168–356 form 27 blocks of seven teams corresponding to
nation slots 5–31. The nine league ranges retain their edition-specific sizes:
German 18/18/18/18/18 and four 12-team leagues; English 20/20/20/20/10 and four
12-team leagues. The English fifth division is an unplayed pool; no ten-team
fixture generator is invented. Unnamed English amateur slots remain in the
preserved source rather than becoming fabricated clubs.

Text follows ISO 646-DE substitutions and preserves extended bytes as Latin-1
Unicode code points; raw bytes remain available. Player names are kept whole in
`name.last`, without guessing first/last-name splits. Fixture tables describe
draw positions; qualified participants and final fixtures are resolved at the
same initialization phase as in the Classic engine.

## Validation and verification

`--validate` checks the profile, versions, safe paths, unique entity/asset IDs,
core references, squad/stage counts, complete cup bindings, contract ownership,
initialization contract, media shapes, portrait indices, resource files and the
inventory before accepting a world. Complete imports also carry a SHA-256 seal,
verified by this tool when present and required by the 0.2 Classic runtime.
ZIP and directory readers enforce size
limits; directory symlinks and injected `state/` content are rejected. CRC32
catches accidental damage; it is not a signature or proof of trustworthy content.
This is the Classic profile validator, not a general competition engine or a full
JSON Schema implementation. Draft 2020-12 schemas are provided for external tools.

`obw_import_tests` uses synthetic data to cover both editions, binary round trips,
frozen appearances, malformed input and damaged payloads. With `BMH_DATA`,
`obw_classic_installation_tests` imports an installation, writes/reopens its ZIP,
reconstructs the startup inputs without reading the source again, and compares
first-season initialization bytes and both RNG streams. It also compares data
regions, atlas pixels, palette, fonts and audio against the source. The same test
can be run with a Football Limited directory as its argument. It does not test
OBW integration in a released client or a complete played season.

## Partial development export

`--database-only` deliberately retains the previous `legacy-database-v1` export
for database investigations without media. Its manifest remains
`playable: false`, requires `import:legacy-database-v1`, and its report lists gaps.
It is **not** one of the completed Classic mods. Default imports never silently
fall back to this mode.

## Rights and excluded data

All imported content remains local and retains its original ownership. Do not
publish generated worlds or assets without appropriate rights. Public tests and
schemas contain only independently authored implementation and synthetic data.

The standalone installation converter does not migrate careers or personal
settings. The 0.2 launcher separately migrates supported `.FBM`, `.EIN` and `.MEN`
files into native personal files; see its [migration contract](runtime-0.2.md).
`SOUND.CFG`, high scores, backups, editor/program executables and external manuals
are not imported. In-game readme content is retained when available. Manager
choices and career RNG state belong to a new game or `.obs` save, not to this
installation conversion.
