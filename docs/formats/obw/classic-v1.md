# Classic installation content contract, version 1

This contract completes the installation import for the two supported Classic
mods without making the 0.1 engine consume OBW. It is an additive OBW 1.0
extension, guarded by **`requires: ["runtime:classic-v1"]`**. Readers which do not
implement it must reject it. The mod IDs are `BMH-classic` and `FLTD-classic`.

The contract is a bridge for the 0.2 adapter. Core JSON gives stable identities
and useful typed projections; `x-classic` retains every remaining content byte
and binds initialization semantics to a version. It does not make a legacy cup
table into the generic competition engine planned for 0.4. Neither a DOS
executable nor an installed game is needed to reconstruct the imported content.

## Content and precedence

| Path | Contract |
|---|---|
| `manifest.json` | Profile `classic-installation-v1`, canonical mod ID, content ownership, season reference and required runtime |
| `world/*.json` | Core entities, source squad identities, initialization policies, league/cup projections and rule/calendar projections |
| `x-legacy/source-database.json` | Exact master database as hex, with `legacy-source@1.0`; preserves spare slots and packed fields |
| `x-legacy/database.json` | Named auxiliary tables and name pools |
| `x-legacy/texts.json` | Original text indices, decoded strings and raw bytes |
| `x-classic/startup.json` | `classic-startup@1.0`, edition, layout version, initializers, inputs and resource references |
| `x-classic/{source,effective}/*.bin` | Original and supported-profile DATA regions, never program code |
| `x-classic/{desktop,formation}-seed.bin` | The 864-byte desktop and 1,408-byte formation input records |
| `x-classic/text-blob.bin` | Exact text block after its seven-byte source header, including following bytes used by bounded legacy text reads; startup metadata preserves the header |
| `assets/classic/resources.json` | Image dimensions/indices/PNG paths, bitmap-font metrics, audio-container chunks and optional artwork |
| `assets/classic/palette.json` | All 256 ARGB colors, using the existing verified VGA-DAC expansion |
| `assets/index.json` | Logical keys, paths, kind, source and rights metadata |
| `assets/portrait-sets/bmh-classic@1/set.json` | Four backgrounds, five ordered layers, individual part keys and placement |
| `x-classic/inventory.json` | Every other entry's path, byte count and CRC32 |

`content_complete: true` means that all content needed by this particular runtime
is present. It does not identify a running game, an OBW-capable released client,
or a completely generic typed simulation. The adapter must preserve legacy
fields it does not otherwise use. Names/appearance/assets in the core projection
are the presentation values; original byte names and offsets remain available
for Classic initialization and compatibility. An adapter which supports editing
ratings, membership or competitions must explicitly apply those changes to its
initialization model; blindly replaying the preserved database would ignore them.

## Data regions

`openbmh-classic-layout@1` fixes the address layout already documented in
`src/formats/executable_layout.hpp` at the introduction of this contract:

- Common German-layout data: `[0xA0000, 0xC1000)`.
- Football Limited only, native English data: `[0x6B000, 0x776A8)`.

Only these allowlisted regions and four bounded text literals can be reconstructed.
The literals are the free-kick, penalty, corner and automatic-taker labels at
`0x80CC9`, `0x80CD2`, `0x80CDB`, `0x80CE3`, with bounds 9/9/8/5 bytes including
the terminator. They share a German LE code object, so they are extracted as
individual strings, never as the containing object. English normalized images
retain their existing values (empty where untranslated). All other locations of
the compatibility image are zero. DOS headers, program instructions and relocation
instructions are excluded. Pointer-valued data remain numbers; they are never executed.

`effective` is the existing loader's output with `openbmh-supported@1` rules.
`source` is the same loaded/translated data before applying that rule profile.
English translation still maps the edition's content into the common layout and
also retains its native data region. The two copies prevent shared rule
normalization from destroying source values. The current supported rule profile
is the default for both Classic mods; selecting unpatched rules requires a
separately versioned runtime/profile, not a silent behavioral change.

Raw regions include all content read through the current executable-data API:
cup/qualification tables, ladder, prices, training and credit data, stadium
configuration, player-generation settings, match parameters, embedded strings,
interface atlases/coordinates and audio tables. Some of these remain byte-based
compatibility fields rather than final generic rule objects. They are fully
preserved, not left to a future re-import from the executable.

## New-game initialization

The importer does **not** choose a manager, start a career, simulate a season,
consume either game RNG or import an FBM. The adapter receives two seeds, manager
choices, level, mode and competition options when starting a new game. It applies
these operations in order, retaining the existing engine's arithmetic, packed
field behavior and draw sequence:

| Initializer | Existing semantic operation |
|---|---|
| `classic-load-seeds@1` | `startup_state`: database/pools, saved static blocks, desktop/formation input, competition pointers and nationality transforms |
| `classic-player-ratings@1` | `startup_players`, with edition league structure and original extra-player slots |
| `classic-pending-offers@1` | Clear the four pending-offer words to their empty sentinel, as `new_career_before_dialog` |
| `classic-markets-and-moods@1` | `new_career_markets_and_moods`: funds/shares, provisional computer-manager names, market history and club moods |
| `classic-free-agents@1` | `new_career_players`: ages/skills and release of extra players |
| `classic-setup-options@1` | Apply level, mode, duration, manager count, competition/calendar masks, then `career_managers_begin` |
| `classic-confirm-managers@1` | `career_add_manager`: name/portrait, identity swaps, ratings, financial/contractual, lineup, stadium and sponsor initialization; then `career_managers_end` |
| `classic-season-setup@1` | `career_season_setup` and `career_first_steps`: entrants, international cycle, calendar, tickets, club accounts, player season and shuffled draw positions |

Extra runtime configuration mirrors `CareerSession`: currency suffix, training-camp
data, credit tables/divisor, edition scheduling, message strings and runtime
options all come from the imported content/current versioned Classic algorithm.

The adapter maps `manifest.start_date` to its calendar's season origin. The
current 0.1 engine uses a 2026 date label internally; the importer does not change
that engine. The installation equivalence test uses the same 2026 origin and
identical options/seeds on both paths. Other requested start years are retained
as world input for the 0.2 calendar adapter.

`classic-manager-finance@1`, `classic-player-contract@1` and
`classic-stadium@1` are **initialization policies**, not existing contracts,
balances or owned stadiums. Finance policies cover 138 league clubs; actual
human-manager accounts are created when a manager is assigned. Contract seeds
bind every assigned player to the source team with the packed source values.
Referees are staff entities; managers and other generated staff get identities
when a new game creates them. Stadium entities form the complete name pool.
No invented birth date, contract term or zero-capacity stadium fills a gap.

## Competition and calendar projections

All nine leagues use the edition's sizes, members and fixture draw-position
tables. German regional third divisions remain parallel. The English ten-club
fifth tier remains unplayed, with no invented fixture generator.

All 36 source cup entries remain in the data regions. Entries with a valid
competition ID 0–26 are also represented as stages grouped by competition ID;
sentinel entries remain data only. Their Classic fields record source entry,
flags, next/parallel links, round counts, qualification quota and group number.
Slots with `resolver: classic-entry@1` are dynamic fields populated by
`classic-season-entrants@1`, not explicit team lists. `teams` records the source
stage size and can be zero before initialization. The resolver must implement
the current edition's selection and qualification algorithm, including FA Cup
qualifying/proper rounds and League Cup for English data, or indoor/Masters for
German data. The generic `format` field alone is insufficient to execute a stage.

For inspection, `classic-season-entrants@1` assigns each first-division champion
to the Champions League. A holder in that league also qualifies (or the
runner-up if already champion). Domestic cup winners, with finalists as
replacements, enter the Cup Winners' Cup. Title-holder overlap affects the
remaining allocation. The regular UEFA-Cup quotas are `[4,4,4,4,3]` for source
league indices `[0,5,6,7,8]`, taking the next unqualified clubs by rank. Foreign
cup representatives follow the Classic selection procedure. The historical
`x-legacy.europe_places` field is a prediction/colouring value, not that quota.

The versioned Classic ladder uses its first four domestic groups. Relegation
counts come from each league's `x-legacy.relegation_places`. A single lower
league supplies the corresponding top-ranked clubs; parallel lower leagues
supply champions plus winners of the Classic playoffs. Without that playoff
result only the champions are exchanged. The last domestic group draws from
the newcomer pool, never a foreign league named by an unused `below` entry.
License withdrawals and the Classic protection of a human manager at the
bottom tier can affect the actual result. Foreign Classic leagues have no
regular promotion/relegation. These are compatibility policy summaries, not
independent typed modern rule declarations or permission to simulate them in
the editor.

`classic-calendar@1` retains the complete source day table and bindings.
`classic-season-calendar@1` applies options, season cycle, weekday alignment and
leap-year behavior using the Classic calendar algorithm at runtime. Cup series
bind calendar bit `competition_id + 5`; league series retain the edition's
calendar-group binding. Dated fixtures cannot be exported before manager/options
selection without changing the original start semantics.

## Artwork, portraits, fonts and audio

Every imported image has an indexed PNG for standard consumers plus a `.pixels`
payload for the Classic adapter's exact palette-index operations. Dimensions and
palette are explicit. The source VGA files themselves are not required. Crest
keys bind stable club identities; the original generic crest is used where the
Classic interface deliberately uses a fallback. Kit selectors and the complete
relevant sprite sheets remain available.

`appearance` contains the source-name-derived result, computed with the same
signed-byte/unsigned-seed arithmetic as `PlayerCardScreen::draw_portrait`, including
its discarded initial draw. It never consumes the game's RNG streams. A rename
changes the name, not the recipe. The set stores each part separately, preserving
linear source-stride wrap at row edges, transparent index zero, layer order and
offsets relative to the 38×38 background. It never stores composed faces.

Bitmap fonts store glyph locations, widths, advances, original alphabet bounds,
baseline and exact bitmap bytes. The zero-width replacement glyph may point
beyond the bitmap because it is never dereferenced. These fonts use the Classic
byte encoding; new modern interface fonts remain a separate theme concern.

The four original ALPTRAUM audio containers are local binary assets with chunk
metadata, consumed by the already implemented audio decoder. Music keeps its
instrument/sequence data rather than being destructively flattened to a recording.
Source frequency/operator tables remain in the Classic data regions. Optional
FLI/FLC and editor artwork is retained as identified local artwork, without
importing editor programs, user settings or saves.

## Reader, writer and checks

`bmh::obw::read` accepts ZIP/directory content and validates it. `load_classic`
returns the read-only input model (`Database`, data-only `LeImage`, full `TextDb`,
desktop/formation seeds, indexed images, palette, fonts and audio) independently
of either game frontend. It does not create `GameState` or `CareerSession`.

Binary payloads are byte strings, not UTF-8 JSON strings. Paths are authored
relative forward-slash paths. The reader rejects traversal, duplicate entries,
symlinks in a directory, excessive sizes and unknown required profiles. The
validator checks cross-references, complete stage/contract bindings, resource
shapes, portrait indices and inventory. CRC32 detects accidental damage, not
malicious modification. An editor deliberately changing a world must update its
inventory (`refresh_classic_inventory`) and validate it before writing.

The installation test reconstructs from an OBW ZIP, then compares a complete
first-season initialization in RAM against the existing legacy-load path:
native state bytes and both RNG seeds/counts. It also compares every exported
data-region byte and original atlas pixels, palette, fonts and audio. This proves
content/startup preservation for the supplied installation and chosen test start;
it does not claim an implemented 0.2 adapter, native-platform testing, a fully
played season or savegame conversion.
