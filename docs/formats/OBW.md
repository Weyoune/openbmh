# OBW: OpenBMH world, save and pack format

## Implemented profiles

| Profile | Format | Reader/writer | Playable |
|---|---|---|---|
| `classic-installation-v1` | OBW 1.0, `runtime:classic-v1` | Complete BMH/FLTD converter; OpenBMH 0.2 launcher, GUI and console | Yes, sealed Classic content |
| `editor-world-v1` | OBW 1.1, `editor:world-v1` | OpenBMH Editor 0.5: OBW/unpacked worlds and legacy import, OBW-only saving | No; editable start database |
| `legacy-database-v1` | OBW 1.0 development export | Converter's explicit `--database-only` option | No; partial legacy export |

`openbmh_import` produces `BMH-classic` and `FLTD-classic` worlds including
startup inputs, media and frozen portrait recipes. The
[import guide](obw/import.md) distinguishes them from the partial development
export. OpenBMH 0.2 reads the sealed content without the original installation
and writes separate native career checkpoints; see the
[runtime contract](obw/runtime-0.2.md).

The [1.1 editor profile](obw/editor-v1.md) adds editable worlds without relaxing
Classic seals. Opening Classic content in the editor preserves its sources;
saving creates an editor world rather than a newly sealed Classic world.
Names such as “Modern” are display names, not proof of runtime compatibility.
A modern-season data package can be structurally valid and fully editable while
its competition/season engine is still pending. See [the editor guide](../EDITOR.md).

The sections below define the general format and distinguish implemented
profiles from future capabilities. Generic calendar/rule authoring, modern
season simulation, network play and generic world-to-career generation are
not implemented merely because their data structures are documented here.

## 1. Goals and non-goals

Goals:

- One documented, versioned format for a game world (start database), a running career (save) and content packs.
- A typed model of nations, clubs, players, staff, contracts, finances and competitions that no longer depends on the memory image of a particular executable.
- Data-driven competitions: up to **32 leagues** (national and foreign leagues together), real-world European club cups (league phase plus knockout), and national-team competitions including qualification and a Nations-League-style tiered competition.
- Import from the supported German editions and from Football Limited (FLTD), both for a new career and for an existing `.FBM` save.
- Preparation for a high-resolution interface built with RmlUi: logical asset keys, high-resolution crests and icons, player photos, further languages and modern font formats.
- Forward compatibility: readers of the same major version accept files written by later minor versions.

Non-goals of version 1.0:

- The match engine and its parameters (reserved namespace `x-matchengine`, see §3.3).
- Network play and a binary encoding (reserved, see §2.4). The separate editor
  uses the additive 1.1 profile described above.
- Layout or screen data. Interfaces are themes (§9.6), not part of a world.

## 2. Container and files

### 2.1 Container

Every OBW file is a ZIP archive (deflate or stored) containing UTF-8 JSON documents and referenced binary assets. The 1.1 editor reader also supports ZIP64, with its own documented size limits. The versioned Classic extension additionally permits data-only compatibility regions and startup input records; it does not permit executable code or save state in a world. The readers use miniz for the container; the editor additionally uses its declared JSON/image dependencies. Generic editor JSON remains editable by hand or script; sealed Classic packages must retain their original bytes and seals.

| Extension | `kind` | Contents |
|---|---|---|
| `.obw` | `world` | Start database: everything needed to begin a career |
| `.obs` | `save` | A world plus the running state of a career |
| `.obp` | `pack` | Overlays and assets for worlds (names, crests, photos, texts) |
| `.obt` | `theme` | Interface theme for the RmlUi client (§9.6) |

An unpacked directory with the same layout is accepted wherever an archive is, which keeps development and version control simple.

### 2.2 Layout

```
manifest.json            required, §3
world/nations.json       nations and their national teams
world/clubs.json         clubs (teams of type "club")
world/players.json       players
world/staff.json         managers, coaches, other staff
world/stadiums.json      stadiums
world/contracts.json     player and staff contracts
world/finance.json       club finances, sponsors, advertising, loans, shares
world/competitions.json  competition definitions, §6
world/calendar.json      season calendar, §7
world/rules.json         rule values (training camps, interest rates, ...)
state/...                save only, §5.9
locale/<lang>/*.po       texts, §9.3
assets/...               local Classic worlds, packs and themes, §9
x-<name>/...             extensions, §3.3
```

Every table file is an object `{"schema": "<table>@<major>.<minor>", "items": [...]}`. Tables with many rows (results, history, mail) may instead be stored as JSON Lines (`*.jsonl`, one item per line, first line the header object without `items`).

### 2.3 Encoding rules

- Text is UTF-8. Legacy single-byte text is converted on import (`iso646de_to_utf8`).
- Money is an integer in the smallest unit of the world currency (`manifest.currency`, e.g. `{"code": "DEM", "minor_units": 0}`); no floating point for money.
- Dates are ISO 8601 calendar dates (`"1995-07-01"`), in-game time is a date plus an optional `"minute"`.
- Percentages and ratings are integers on the scale declared by their attribute set (§5.3).
- Optional fields may be omitted; the schema gives their default.

### 2.4 Reserved

A compact binary encoding of large `state/` tables is reserved for a later minor version as an alternative representation of the same schema (`"encoding": "cbor"` in the table header). Readers of 1.0 reject unknown encodings with a clear error.

## 3. Versioning and migration

### 3.1 Manifest

```json
{
  "format": "openbmh-world",
  "format_version": "1.0",
  "kind": "world",
  "min_reader": "1.0",
  "generator": "OpenBMH 0.2.0",
  "created": "2026-10-17",
  "title": {"default": "Season 1995/96"},
  "provenance": "import:bmh-de",
  "content_license": "local-import-only",
  "currency": {"code": "DEM", "minor_units": 0},
  "start_date": "1995-07-01",
  "languages": ["de"],
  "limits": {"leagues": 32},
  "requires": [],
  "extensions": []
}
```

- `format_version` is `major.minor`. A **minor** step only adds optional fields, tables, enum values that have a documented fallback, or new stage types guarded by `requires`. A **major** step may rename, remove or re-interpret fields.
- `min_reader` is the lowest reader version that can use the file correctly (a writer raises it when it uses a minor feature without a safe fallback).
- `requires` lists features without which the file cannot be played, e.g. `["stage:swiss"]`; a reader that lacks one refuses the file instead of guessing.
- `provenance` records where the content came from: `native`, `import:bmh-de`, `import:bmh-de-classic`, `import:fltd`, or `pack:<id>`. `content_license` is free text or an SPDX identifier; `local-import-only` marks data converted from a user's own installation (§8.4).

### 3.2 Reading and migration

- Readers ignore unknown fields and tables, keep them in memory and write them back unchanged (round trip), so a newer minor version survives an older tool.
- Missing optional fields take the schema default.
- Older major versions are upgraded on load through a chain of migrations (`migrate_1_to_2`, `migrate_2_to_3`, ...). Writers always write the newest version; a migrated save is written under a new name unless the player confirms the upgrade.
- Each table carries its own `schema` version, so a table can advance a minor step independently; the manifest version is the highest of them.

### 3.3 Extensions

Fields and directories whose name starts with `x-` belong to extensions (`x-matchengine`, `x-realnames`, `x-editor`). The core never interprets them, preserves them on round trip and lists active ones in `manifest.extensions` with a version, e.g. `{"id": "x-matchengine", "version": "1.0"}`.

### 3.4 Schemas

Each format version is published as JSON Schema (draft 2020-12) under `docs/formats/obw/schema/<major>.<minor>/`, one file per table. The schemas are the machine-readable specification and the basis of the validator (§10).

The schemas describe the intermediate `legacy-database-v1` profile and the completed `classic-installation-v1` content profile. The latter requires `runtime:classic-v1`: it contains initialization policies and data-region compatibility fields in addition to typed projections. A generic OBW reader cannot execute those fields without the Classic adapter. Save, pack, theme and fully generic playable-world schemas remain future work. Optional fields and unknown extensions remain allowed.

## 4. Identifiers and names

- Every entity has a stable string id `<type>:<key>`, e.g. `nation:de`, `club:0042`, `player:01873`, `comp:de-1`, `stadium:0042`. Keys are opaque: they never encode a real name, so a world and a pack can be published independently.
- Ids are unique within a world and never reused within a save. The engine assigns runtime indices; files never store them.
- Nation keys use ISO 3166-1 alpha-2 where one exists, otherwise a documented private key (`nation:x-eng` for England, `nation:x-sco`, ...).
- Every displayed name is a localized string: `{"default": "...", "en": "...", "short": {"default": "..."}}`. `default` is required; a language entry overrides it; `short` and `abbr` are optional display variants for narrow layouts.
- Packs overlay names, crests and photos by id (§9.5), never by position.

## 5. Entities

Field lists name the type and whether the field is required (R) or optional (O, with default). Only the main fields are listed; the schema is authoritative.

### 5.1 Nation (`world/nations.json`)

| Field | Type | | Meaning |
|---|---|---|---|
| `id` | string | R | `nation:<key>` |
| `name` | localized | R | |
| `confederation` | string | R | `uefa`, `conmebol`, `caf`, `afc`, `concacaf`, `ofc`, `other` |
| `coefficient` | integer | O 0 | Club-competition ranking points (§6.6) |
| `strength` | integer | O 0 | Rating of the national team when no squad is simulated |
| `national_team` | string | O | Team id of type `national` (§5.2) |
| `playable` | bool | O false | Whether its leagues are simulated in full |

### 5.2 Team (`world/clubs.json`)

Clubs and national teams share one entity type, distinguished by `type`.

| Field | Type | | Meaning |
|---|---|---|---|
| `id` | string | R | `club:<key>` or `nt:<nation key>` |
| `type` | enum | R | `club`, `national`, `amateur` (newcomer pool), `foreign` (strength only) |
| `name` | localized | R | |
| `nation` | string | R | |
| `stadium` | string | O | |
| `colors` | array | O | 1 to 3 `#rrggbb` values, used for generated crests and kits |
| `reputation`, `fans`, `strength` | integer | O | Club values on the attribute scale of the world |
| `founded` | integer | O | Year |
| `crest` | string | O | Asset key, default `asset:club/<key>/crest` (§9.1) |
| `x-*` | any | O | Extensions |

Teams without a squad (`foreign`, or `national` in a world that does not simulate national squads) carry `strength` instead of players.

### 5.3 Player (`world/players.json`)

| Field | Type | | Meaning |
|---|---|---|---|
| `id` | string | R | |
| `name` | object | R | `{"first": "...", "last": "...", "known_as": "..."}`, each a plain string; `known_as` optional |
| `born` | date | R unless `age_at_start` | Age is derived from the in-game date |
| `age_at_start` | object | Alternative to `born` | `{"years": 22, "on": "2026-07-01"}` when a legacy database gives an age but no exact birth date. Never invent a birthday. A consumer must explicitly support this precision. |
| `nationalities` | array | R | Nation ids, first entry is the sporting nationality (national-team eligibility) |
| `positions` | array | R | `gk`, `def`, `mid`, `fwd` in the classic set; finer positions in later attribute sets |
| `foot` | enum | O | `left`, `right`, `both` |
| `attribute_set` | string | O world default | e.g. `bmh-classic@1` |
| `attributes` | object | R | Values named by the attribute set, e.g. `{"strength": 7, "technique": 5, "condition": 9, "form": 6}` |
| `character` | object | O | Traits the attribute set defines |
| `team` | string | O | Current team id; absent for free agents |
| `squad_number` | integer | O | |
| `status` | object | O | Injury, suspension and card counters per competition (save only) |
| `photo` | string | O | Default `asset:player/<key>/photo` |
| `appearance` | object | O | Recipe of a composed portrait, see below |

**Composed portraits.** The original game stores no picture per player: it seeds a small random generator from the player's name and picks one of four backgrounds and one part for each of five layers from a parts sheet of the installation. OBW stores the outcome as a recipe instead of an image:

```json
"appearance": {"set": "bmh-classic@1", "background": 2, "layers": [5, 0, 3, 7, 1]}
```

- `set` names a portrait part set; it defines the number of backgrounds, the layers, the number of parts per layer and their placement. `bmh-classic@1` is the set of the original game.
- The importer computes the recipe once from the legacy name and **freezes** it. A later rename (for example by a name pack) keeps the face; the legacy name-derived generator is never re-run.
- Players created in OpenBMH (youth, generated players) receive a random recipe of the world's default set.
- The recipe holds indices only. The part graphics come from the user's installation or from a pack that provides the same set, possibly in higher resolution (§9.2), so a recipe does not embed a composed face. A local Classic import may include the original part set as local-only assets; distributable worlds need separately licensed artwork.

An attribute set is declared once in `world/rules.json` with its fields, scales and meaning, so a later, finer model can sit next to the classic one without breaking the format:

```json
{"id": "bmh-classic@1", "fields": {
  "strength": {"min": 1, "max": 10},
  "technique": {"min": 1, "max": 10},
  "condition": {"min": 0, "max": 100},
  "form": {"min": 1, "max": 10}}}
```

The exact list and scales of `bmh-classic@1` are fixed when the importer is written, from the fields the current engine uses (`Player` in `src/game/state.hpp`).

### 5.4 Staff and managers (`world/staff.json`)

Managers (human or computer), coaches and other employees: `id`, `name`, `born`, `nation`, `role` (`manager`, `coach`, `physio`, ...), `team`, `attributes`. In a save, `state/managers.json` adds the human manager profile: options, ratings, history, automation settings.

### 5.5 Contracts (`world/contracts.json`)

`id`, `person` (player or staff id), `team`, `start`, `end`, `salary` (money per season), `bonuses` (signing, per goal, ...), `release_clause` (O), `options` (O). One active contract per person and team; the history is kept in the save.

### 5.6 Stadiums (`world/stadiums.json`)

`id`, `name`, `capacity` split by area (`standing`, `seated`, `boxes`), `facilities` (floodlights, heating, scoreboard, ... as named levels), `pitch`, `owner` (team id or `null`).

### 5.7 Finance (`world/finance.json`)

Per team: balance, ticket prices, sponsors (shirt, boards, with duration and value), advertising, loans (amount, rate, due date), shares and other income or cost sources the engine models today. Rate tables such as the credit table live in `world/rules.json`.

### 5.8 Rules (`world/rules.json`)

Named rule values that are content of the world rather than of the engine: training-camp resorts (price, quality grades), credit-rate tables, attribute sets, points for a win, squad limits. Today these values are read from the executable of the selected edition; in OBW they are ordinary data (and the starting point of the configurable rules of roadmap 0.3).

### 5.9 Save state (`state/`, saves only)

| File | Contents |
|---|---|
| `state/career.json` | Current date, season, phase, active human managers, settings |
| `state/rng.json` | State of the random generator(s) (`docs/RNG.md`), so a load continues deterministically |
| `state/tables.json` | Standings per competition stage and group |
| `state/results.jsonl` | Every played or scheduled match: competition, stage, round, home, away, result, after extra time / penalties |
| `state/squads.json` | Lineups, formations, tactics per team |
| `state/history.jsonl` | Season history: champions, cup winners, top scorers, records |
| `state/mail.jsonl` | Messages and news |
| `state/market.json` | Transfer list, open bids, loan offers |
| `state/x-legacy.json` | Unmapped legacy values carried over by the importer (§8.3) |

## 6. Competitions

### 6.1 Model

A competition is an ordered list of **stages**. Each stage takes teams from **slots** (§6.4), plays a format and hands teams on (to the next stage, to another competition, or to a final ranking).

```json
{
  "id": "comp:de-2",
  "name": {"default": "2. Liga"},
  "type": "league",
  "nation": "nation:de",
  "tier": 2,
  "trophy": "asset:comp/de-2/trophy",
  "season_cycle": 1,
  "stages": [{
    "id": "regular",
    "format": "league",
    "teams": 18,
    "legs": 2,
    "points": {"win": 3, "draw": 1, "loss": 0},
    "tiebreakers": ["points", "goal_difference", "goals_for", "head_to_head", "draw_lots"],
    "matchdays": "calendar:league-weekends"
  }],
  "movement": [
    {"ranks": [1, 2], "to": "comp:de-1"},
    {"ranks": [3], "to": "comp:de-1/playoff", "role": "challenger"},
    {"ranks": [16, 18], "to": "comp:de-3"}
  ]
}
```

`type` is `league` (counts towards the league limit), `cup` (national cup), `club_international`, `national_team`, `friendly` or `indoor`.

### 6.2 Stage formats

| Format | Purpose | Main parameters |
|---|---|---|
| `league` | Round robin | `teams`, `legs` (1, 2, n), `points`, `tiebreakers`, fixture generator |
| `groups` | Several round robins | `groups`, `teams_per_group`, `legs`, seeding `pots`, draw constraints (e.g. no two teams of one nation) |
| `swiss` | League phase with a fixed number of games against drawn opponents | `teams` (e.g. 36), `matches` (e.g. 8), `pots`, `home_away_balance`, one common table |
| `knockout` | Ties | `legs` (1 or 2), `seeded`, `draw` (`open`, `seeded`, `bracket`), `extra_time`, `penalties`, `away_goals` (on/off), `neutral_venue`, `replay` (FA-Cup style) |
| `playoff` | Ties between teams of different competitions (relegation, qualification) | as `knockout` plus the roles of the participants |
| `final` | Single match or final tournament | `venue`, `teams` |

Common stage parameters: `extra_time` (minutes), `penalties`, `suspensions` (cards that lead to a ban, counted per competition or per stage), `substitutions`, `squad_registration` (size, home-grown quota), `prize_money` per round and result, `tv_money`, `gate_split`. New formats are added through a minor version with an entry in `manifest.requires`.

### 6.3 Fixture generation

A `league` or `groups` stage names a generator: `"generator": "round-robin"` (circle method, home/away balanced) works for every size. The legacy round tables of the original game remain available as `"generator": "legacy:<teams>"` for imported worlds, so imported careers keep their familiar order; they come from the user's installation, not from the format.

### 6.4 Slots and qualification

Participants are declared, not hard-coded:

```json
"slots": [
  {"from": "comp:de-1", "ranks": [1, 4], "stage": "league-phase"},
  {"from": "comp:de-cup", "winner": true, "to": "comp:uel", "stage": "league-phase",
   "fallback": {"from": "comp:de-1", "next_unqualified": true}},
  {"from": "coefficient", "nation_rank": [1, 4], "places": 4},
  {"holder": "comp:ucl", "stage": "league-phase"}
]
```

A slot source is: a rank range of a stage, the winner/runner-up of a competition, the holder of a title, the next team not yet qualified (substitution rules), a coefficient ranking, or an explicit list. The engine resolves all slots at season end in the order given by `world/calendar.json`.

### 6.5 League pyramid

`movement` on a league connects tiers: promotion, relegation and playoffs between two leagues, several parallel lower leagues feeding one league (the two regional third divisions of the German editions), and a refill pool for the lowest tier (`"refill": {"from": "pool:amateur", "count": 2}`). This replaces the fixed per-edition tables in `src/game/schedule.hpp` (`kLeagueRelegation`, `kLeagueEurope`, `kGroupBelow`, `kNewcomerFirst`).

### 6.6 Coefficients

`world/rules.json` may define coefficient rankings (club and nation) with the points per result and per round reached and the number of seasons summed. They drive seeding pots and the number of European places per nation. A world may instead fix places per nation (`"places": {"nation:de": {"ucl": 4, "uel": 2}}`), which is how imported legacy worlds start.

### 6.7 Templates

The project ships competition templates under `docs/formats/obw/templates/`. They contain structure only, with neutral names:

- **Club competitions:** a champions cup with a 36-team league phase, a knockout playoff for places 9 to 24, round of 16 to final; a second and a third club cup in the same shape; a super cup; the knockout-only cups of the original game.
- **National teams:** a world cup with continental qualification groups and playoffs, a continental championship with qualification, and a **tiered nations league**. The nations league has leagues A to D with groups of three or four, promotion and relegation between tiers, a final tournament for the group winners of A, and playoffs that can feed continental-championship places.
- **National cups and side competitions:** knockout national cups (with or without replays and a qualifying round), an indoor tournament, friendlies.

### 6.8 National teams

A world that simulates national teams gives each `national` team a call-up rule: squad size, eligibility through `nationalities[0]`, selection by attribute set (best `n` by position) or by a human manager. International windows in the calendar (§7) suspend club play when the world asks for it (`"club_break": true`). Worlds without national squads use `strength` and simulate only results.

## 7. Calendar

`world/calendar.json` describes a season as dated events, independent of the executable's day table:

```json
{
  "season_start": "07-01",
  "season_length_days": 365,
  "windows": [
    {"id": "summer-transfer", "type": "transfer", "from": "07-01", "to": "08-31"},
    {"id": "intl-sep", "type": "international", "from": "09-02", "to": "09-10", "club_break": true}
  ],
  "series": [
    {"id": "league-weekends", "weekday": "sat", "from": "08-08", "to": "05-20", "skip": ["winter-break", "intl-*"]},
    {"id": "ucl-league-phase", "weekdays": ["tue", "wed"], "dates": ["09-16", "09-30"]}
  ],
  "events": [
    {"date": "06-30", "action": "season_end"},
    {"date": "01-05", "action": "training_camp_window"}
  ]
}
```

Dates are month-day within the season; the engine maps them to concrete days each year, including leap years and seasons whose first day falls on a different weekday (the cases fixed in the current calendar code). Stages refer to a series by id. Imported worlds receive a calendar generated from the edition's day table, so the familiar rhythm stays.

## 8. Import from BMH and Football Limited

### 8.1 Sources

The importer (planned module `src/import/`) reuses the existing loaders:

| Source | Loader | Result |
|---|---|---|
| Master database `M3D.MAN` / `M3E.MAN` | `src/formats/m3d.cpp` | Clubs, players, staff, stadiums |
| Executable tables (league sizes, ladders, calendar, fixture tables, rule values) | `src/formats/le.cpp`, `src/launch/fltd.cpp`, `src/game/schedule.hpp` installers | Competitions, calendar, rules |
| Text database `TEXTD.BLK` / `TEXTE.BLK` | `src/formats/textd.cpp` | Local language layer `de-original` / `en-original` |
| Save `*.FBM` | `src/formats/fbm.cpp`, `savestate.cpp` | `state/` of a save |
| Crests and pictures `*.VGA`, `WAPPEN.LBM` | `src/formats/vga.cpp` | Low-resolution fallback assets, local only |

Edition detection is the one the launcher already performs (`src/launch/game_data.cpp`).

### 8.2 Mapping per edition

The standalone converter and its Classic contract are documented in [the edition mapping](obw/import.md) and [Classic v1](obw/classic-v1.md). The core league mapping is:

| Legacy league | German editions | Football Limited | OBW |
|---|---|---|---|
| 0 | 1st division, 18 teams | 1st division, 20 teams | `comp:<n>-1`, tier 1 |
| 1 | 2nd division, 18 | 2nd division, 20 | tier 2 |
| 2, 3 | Two parallel third divisions A and B, 18 each, one promotion group | 3rd and 4th division, 20 each | tier 3 (two leagues) / tiers 3 and 4 |
| 4 | Fourth tier, 18 | Fifth league, 10 teams, not played | tier 4 / tier 5 with `"played": false` |
| 5-8 | Four foreign leagues of 12 | Same four foreign leagues of 12 | `comp:<nation>-1` each, `nation.playable = true` |
| Newcomer pool | 30 amateur clubs | 12 further named clubs | `pool:amateur` |

Cups, European cups, the indoor tournament, the English FA Cup with its qualifying stage and the League Cup become competitions with the same rules the engine applies today. Relegation places, European places and promotion groups are taken from the installed tables (`kLeagueRelegation`, `kLeagueEurope`, `kGroupBelow`), not re-typed by hand.

### 8.3 Fidelity

The complete `classic-installation-v1` importer preserves all installation content without starting a career. Typed projections are complemented by the versioned `x-classic` startup contract and data-only regions, including original and effective rules. Installation tests reconstruct from OBW and compare first-season initialization bytes and both RNG streams with the existing loader. They do not establish a released OBW frontend or save continuation. The optional `--database-only` intermediate profile remains explicitly non-playable. `state/x-legacy.json` is reserved for converted saves.

- A new career imported from an installation must produce the same first season in the engine as the legacy start (same fixtures, same participants), verified by the regression runs.
- A converted `.FBM` must continue with the same results as the legacy save for a fixed number of turns.
- Values the typed model does not yet understand go to `state/x-legacy.json` with their legacy name, so nothing is lost while the model grows area by area.

### 8.4 Rights

Imported content stays on the player's machine. The importer marks it `"provenance": "import:..."` and `"content_license": "local-import-only"`; tools refuse to bundle such a world into a distributable pack. Only worlds and packs with their own licence may be shared.

## 9. Assets, languages, fonts and the RmlUi interface

### 9.1 Asset keys

Data never refers to file paths. It uses logical keys:

| Key | Meaning |
|---|---|
| `asset:club/<key>/crest` | Club crest |
| `asset:club/<key>/kit/home` | Kit |
| `asset:player/<key>/photo` | Player photo |
| `asset:staff/<key>/photo` | Staff photo |
| `asset:comp/<key>/logo`, `asset:comp/<key>/trophy` | Competition logo and trophy |
| `asset:nation/<key>/flag` | Flag |
| `asset:stadium/<key>/image` | Stadium picture |
| `asset:icon/<name>` | Interface icon |

**Competition trophies.** Every competition type may set the optional string
`trophy` to a logical key `asset:comp/<key>/trophy`. The key is stable and opaque;
it may be shared by several competitions, and never embeds a season, localized
name, file path, URL or runtime index. `<key>` is a nonempty sequence of ASCII
letters, digits, `.`, `_` or `-`, excluding `.` and `..`. A competition without
the field uses `asset:comp/<competition key>/trophy`, where the competition key is
its `id` without `comp:`. This also applies to additional foreign leagues and
international competitions; a fixed competition list is not part of the format.

Resolve that key from active packs by priority, then the world. If unavailable,
resolve `asset:comp/generic/trophy` through the same sources, then use the
client's built-in neutral trophy icon. The generic key is reserved for this
fallback. A missing trophy never prevents a competition from being played.
An invalid explicitly supplied key is an input error, not a request to guess a
file path. File presence is not mandatory because a pack or theme may supply
the image later. Asset-index metadata and rights requirements still apply.
Trophies are presentation only; they do not identify competition rules, stages
or historical winners.

This optional-field clarification remains compatible with 1.0 worlds: old
documents omit it, older readers ignore it, and no new `requires` feature is
needed. Classic converters emit the conventional per-competition key without
fabricating a trophy image or changing the original local media inventory.

### 9.2 Resolution and image formats

A resolver looks a key up in the active packs by priority (§9.5), then in the world, then falls back: original low-resolution graphics of the installation where one exists, a crest generated from `colors`, a silhouette for photos, a neutral icon.

- **Crests, logos, flags, icons:** SVG preferred (scales to every resolution). Otherwise PNG with alpha in size steps 64, 128, 256 and 512 px (`crest@128.png`). The resolver picks the smallest step not below the requested size.
- **Photos:** PNG, JPEG or WebP, aspect 3:4, reference size 300×400 px, larger allowed. Faces centred; the interface crops, it never stretches.
- **Pictures** (stadiums, backgrounds): PNG, JPEG or WebP, 16:9 reference 1920×1080.
- **Trophies:** SVG or PNG/WebP with alpha, preserving the full object and native
  aspect ratio. A transparent generic trophy may be provided under the reserved
  fallback key. No real trophy artwork is bundled by the format specification.

Asset metadata sits next to the files in `assets/index.json`: key, files per size, author, source and licence (required).

**Optional visual defaults (1.1).** A world may set
`manifest.presentation: "assets/presentation.json"`. The referenced document has
`schema: "asset-presentation@1"`, an optional `player_photo_fallback` asset key,
`trophy_fallback` (normally `asset:comp/generic/trophy`), and
`youth_photos: {assets: [<photo keys>], selection: "random-on-creation",
fallback: <photo key>}`. All keys resolve through the normal asset index. A
client supporting youth creation chooses once using its saved game RNG and
persists the selected key; it does not reroll a face on display or rename. This
document supplies pictures, never player records, ratings or generation rules.
Readers without this optional feature retain it and use their existing visual
fallbacks. The current editor preserves it without generating youth players.
An explicitly indexed player `photo` is displayed before composing a retained
`appearance` recipe. A visual overlay can therefore keep Classic appearance and
all game data intact while displaying a supplied photo or silhouette. Sealed
Classic rendering and runtime capabilities are unaffected.

**Player pictures** (`asset:player/<key>/photo`) resolve in this order:

1. A photo from an active pack.
2. The player's `appearance` recipe composed from a high-resolution part set (`assets/portrait-sets/<set>/` in a pack, same indices and layer order as the original set, with its own placement table for the larger size).
3. The same recipe composed from the original parts of the installation, scaled up.
4. A neutral silhouette.

Composed pictures may be cached in the user's cache directory; they are never written into a world, save or pack.

### 9.3 Languages

- Interface texts use gettext `.po` files under `locale/<lang>/openbmh.po` (`msgctxt` for context, plural forms, mature translator tools such as Poedit and Weblate). Message ids are stable English keys.
- Data names are localized strings inside the tables (§4); packs can add languages to them.
- The original text databases are imported into a local layer `de-original` or `en-original`, which the classic interface uses. New interfaces use the project's own texts.
- A fallback chain is configured by the player, e.g. `["en-GB", "en", "de"]`.
- Text is shaped as Unicode (RmlUi supports it); no single-byte substitutions outside the legacy layer.

### 9.4 Fonts

- TrueType and OpenType (`.ttf`, `.otf`); RmlUi renders them through FreeType. Variable fonts are allowed.
- A theme declares font families with weights, a fallback chain covering the languages it claims, and the licence of every font file (for example SIL OFL).
- The original bitmap fonts stay for the classic interface only.

### 9.5 Content packs (`.obp`)

```json
{
  "format": "openbmh-world",
  "format_version": "1.0",
  "kind": "pack",
  "id": "pack:example-crests",
  "title": {"default": "Example crests"},
  "targets": [{"world_provenance": "import:bmh-de"}],
  "priority": 100,
  "content_license": "CC-BY-4.0",
  "overlays": ["names", "assets", "locale"]
}
```

- A pack contains `overlay/<table>.json` files with partial entities by id (only the fields it changes), `assets/` and `locale/`.
- Several packs stack; the higher `priority` wins per field and per asset key. A pack may not add or remove entities in 1.0 (that is a world, not a pack), which keeps saves valid when a pack is removed.
- `targets` restricts a pack to worlds by provenance or world id.
- The project distributes only the format, tools and its own or freely licensed material. Packs with real names, crests or photos are made and shared by their authors under their own responsibility. The team's internal real-name pack uses this format and stays private.

### 9.6 RmlUi themes (`.obt`)

- A theme contains `.rml` documents, `.rcss` style sheets, fonts, icons and its `locale/` texts, plus a manifest naming the client version it targets.
- Interfaces bind to a **view model**, not to the world files. The client exposes data models (RmlUi data bindings) built from the application/protocol layer (`docs/protocol.md`), with ids and asset keys as values. RmlUi loads images through a file interface that resolves `asset:` keys (§9.2).
- Neither worlds nor packs contain layout, so a theme works with every world, and the classic 320×200 interface keeps working next to it.

## 10. Validation and limits

The validator (planned `openbmh_cli --validate <file>`) checks, in this order:

1. Container, manifest, `format_version`, `min_reader`, `requires`.
2. Every table against its JSON Schema.
3. References: every id used exists and has the right type; no id is defined twice.
4. Competition logic: slot counts match stage sizes; movements between tiers are balanced; every stage can be scheduled in the calendar; fixture generators exist for the sizes.
5. Engine limits declared in `manifest.limits` and enforced by the engine of a version. For 1.0:

| Limit | Value |
|---|---|
| Competitions of type `league` (national and foreign) | **32** |
| Teams per league | 4 to 24 |
| Squad size per team | engine maximum of the attribute set |
| Groups per stage | 16 |

The format itself has no fixed limits; a later engine raises them by changing `limits` and its own checks, without a format change.

Errors name the file, the JSON path and the rule (`clubs.json: items[41].stadium: unknown id stadium:0999`).

## 11. Implementation steps

| Step | Roadmap | Content |
|---|---|---|
| 1 | 0.2 | Schemas 1.0, reader/writer, validator, round-trip tests with synthetic worlds |
| 2 | 0.2 | Importer for German editions and FLTD into `.obw`; adapter OBW → current `GameState`; regression: same first seasons |
| 3 | 0.2 | `.FBM` → `.obs` conversion and native OBW saves; area by area the engine reads typed data instead of the memory image (clubs, players, contracts, finances, then competitions) |
| 4 | 0.3 | `rules.json` drives the rule values; balance changes as data |
| 5 | 0.4 | Generic competition engine (stage formats, slots, pyramid, variable league sizes up to the limits); templates for the real-world club cups and national-team competitions |
| 6 | 0.4 | Asset resolver, packs, `.po` languages, RmlUi client with themes |
| 7 | 0.5 | Own worlds that need no original data |

Each step keeps the game playable and the regression runs green; the legacy formats stay readable as import sources.
