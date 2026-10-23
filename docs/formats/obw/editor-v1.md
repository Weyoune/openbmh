# OBW 1.1 editor-world-v1

This additive profile is a native editing interchange. It does **not** change
`classic-installation-v1`, its seals, the Classic runtime or save files.
It is implemented by OpenBMH Editor 0.5.x, whose application version is separate
from the OBW format version and the OpenBMH 0.2.0 game version.

## Identification

```json
{
  "format": "openbmh-world",
  "format_version": "1.1",
  "min_reader": "1.1",
  "kind": "world",
  "title": {"default": "My world"},
  "start_date": "2026-07-01",
  "currency": {"code": "EUR", "minor_units": 2},
  "requires": ["editor:world-v1"],
  "extensions": [{"id": "x-editor", "version": "1"}],
  "x-editor": {"profile": "editor-world-v1", "playable": false}
}
```

The required editor capability is deliberately different from
`runtime:classic-v1`. An edited world must not be resealed as a Classic world.
The display title and a label such as Modern do not grant a runtime capability.
On import the editor keeps the original manifest under
`x-editor.original_manifest`, leaves compatibility/source regions intact, and
requires Save As. An original Classic seal is historical evidence only in this
profile. The existing Classic reader/validator is not broadened to accept it.

## Tables and values

The existing `world/*.json` and `assets/index.json` structure remains. Each table
is an object with `schema` and `items`. Stable `id` identifiers (asset `key`)
are references, never array positions. Existing identifiers and unknown fields
are preserved. New identifiers have a namespace and opaque suffix.

The editor adds explicit club `competition`, competition `members` (club IDs),
and player `squad_order`. These are the editable projections of Classic league
slots/squad rows. Moving a club updates league membership in the same
transaction. The historical Classic slots stay source evidence; a future
playable format adapter must choose and implement its own generation rules.

Player `born` can be `null`; an imported `age_at_start` object remains an age,
not an invented birth date. `team: null` is an unassigned projection; an
`x-editor.membership_unresolved` or `membership_unassigned` marker distinguishes
missing/contradictory source facts from a verified free agent. Reported
nationalities and sporting eligibility remain separate. No full roster is
cut down to the 24 rows of a Classic compatibility view.

Attribute sets declare their actual numeric ranges in `world/rules.json`:
`type: "attribute_set"`, optional `attribute_set` alias, and `fields` mapping
attribute names to `{min,max,...}`. The Classic importer declares strength
0–99, form/condition/technique/bonus 0–127, skills 0–3. An FC set remains a
separate 0–99 system. Missing values are null/absent; `unrated@1` with empty
attributes does not invent a rating.

Competition `trophy` and optional fallback remain logical asset references;
images are indexed just like crests and portraits. Arbitrary new competitions
can use their own trophy or a package-supplied generic trophy.

### Read-only comparisons (Editor 0.5.1)

The editor's `compare` command / `editor_compare` MCP tool projects ratings and
salaries without writing an additional OBW table. Player attributes retain their
set and range. Club and league attributes are player-weighted means of known
values within one set; mixed sets require a filter. Full rosters include reserves,
except source evidence explicitly marked
`x-simulation.squad_role: "source-only-duplicate"`. League membership uses a club's
explicit league, or an unambiguous league `members` reference as fallback.

`contracts.salary` is per season, in integer `manifest.currency` minor units.
Only contracts active on `manifest.start_date` and matching the player's current
team count; absent bounds are open and explicit date bounds are inclusive.
Multiple matching active contracts are ambiguous, not summed. Missing or invalid
salary remains null, independently of an explicit zero. Club/league payrolls
exclude staff and use known player salaries only. `salary_known`,
`salary_unknown`, `salary_ambiguous`, `salary_complete` and `salary_overflow`
describe coverage; a missing/empty/overflowed sum is null. Means of known salaries
round to the nearest minor unit. Attribute means separately report their sample
sizes in `attribute_counts`. No current date, invented contract or foreign-club
strength-band interpretation is substituted for missing data.

### Read-only regulations (Editor 0.5.2)

The `regulations` command / `editor_regulations` MCP tool exposes the same
read-only projection as the Regulations page. It accepts `language` (de/en),
`search`, `nation` and `competition`. It returns `items`, all preserved `rules`,
the world date, `read_only: true` and the session revision. It creates no table,
edits, undo entries or runtime entrants.

Each item retains its full `record` and `stages`. Explicit `movement` entries
retain rank ranges and destination references; tier comparisons label promotion
and relegation, participant roles label playoffs. Competition/stage `slots`
retain their full selector, fallback, explicit destination and phase; they are
visible on both the source and destination competition. Fixed participant lists
are not reinterpreted as qualification rules.

Imported `classic_runtime.qualifications: classic-season-entrants@1` and
`legacy_ladder` records permit a labelled presentation of the existing Classic
policy, documented in [classic-v1](classic-v1.md). Explicit declared rules take
precedence. Unknown rules stay unspecified; the editor never substitutes modern
UEFA quotas or infers qualification from historical colouring fields. This
presentation registry does not implement a new season engine.

Optional `manifest.presentation` references the `asset-presentation@1` visual
defaults described in [OBW §9.2](../OBW.md#92-resolution-and-image-formats).
Youth pools, silhouette and generic trophy keys remain indexed assets. They are
preserved across editor saves, independent of Classic source recipes or seals.

## Sources, conflicts and images

- `x-source/` can contain unchanged or losslessly gzipped normalized source
  tables, input hashes, source-to-projection identifiers and identity evidence.
  References include package-local file paths and JSON pointers. Sources do
  not become authoritative merely by being present.
- `x-editor/review.json` has `items` with `id`, `reason`, `resolved`, optional
  `table`, `entity`, `field` (JSON pointer), and `alternatives` with `value` and
  source descriptions. Choosing a value updates its target plus the review
  record in one undoable transaction. Original alternatives remain.
- Raster and SVG sources are stored unchanged. Identity imports preserve exact
  bytes. Edits use a normalized recipe: `angle` degrees, `flip_x`, `flip_y`,
  `crop: {x,y,width,height}`. Mirroring precedes clockwise rotation; rotation
  expands the canvas; crop coordinates refer to that expanded canvas. Values
  are finite, crop dimensions positive and inside [0,1]. JSON floating-point
  values must survive round trips.
- An edited asset's `x-editor` stores `original`, `original_sha256` and
  `transform`. Originals live in `x-editor/originals/`, rendered files in
  `assets/edited/`, addressed by SHA-256. Raster edits are PNG with transparent
  expanded edges, SVG edits remain vectors. Import does not upscale portraits.
  Referenced external SVG resources are refused. Originals and source recipes
  remain available on re-edit; no repeated destructive resampling is needed.
- Portrait part sets and frozen `appearance` recipes remain valid. The editor
  composes those layers for display; a rename never regenerates the recipe.

## Container, validation and saving

ZIP/ZIP64, stored or deflated entries; UTF-8 JSON; package-local paths without
absolute paths, `..`, backslashes or symlinks. The editor limits are 100,000
entries, 64 MiB per uncompressed entry and 2 GiB total uncompressed bytes. This
is independent of the smaller Classic 1.0 reader limits.

The editor checks identifier uniqueness, typed references, valid dates,
declared attribute ranges and assets. Missing facts/source questions are
warnings; structural errors block saving. A transaction either validates and
commits or is rolled back. `revision` increases monotonically, including
undo/redo. Undoing to the saved state clears the dirty flag.

Unknown entries, extensions, binary assets and untouched table bytes are
retained. Large assets remain lazily read from an immutable source snapshot.
Saving streams a temporary ZIP and atomically replaces the destination only
after finalization. Modified-on-disk sources require Save As. The editor never
writes legacy files or a running save.

The [1.1 editor schema](schema/1.1/editor-world.json) documents structural
constraints. Runtime validation additionally resolves cross-table references
and rule-declared scales. This is not a claim that the generic world can yet
be played by a game engine.
