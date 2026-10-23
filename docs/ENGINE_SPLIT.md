# Engine split: Classic, Classic Extended and Modern

Status: **concept and migration plan**. It prepares one shared match engine under the season engines of three game variants. Nothing in this document changes the behaviour of the current release.

## 1. Goal

OpenBMH will offer three game variants:

| Variant | Data | Season engine | Interface | State |
|---|---|---|---|---|
| **Classic** | Imported from the user's BMH or FLTD installation (OBW `runtime:classic-v1`) | Classic: fixed calendar, 9 leagues, original competition structure and rules | Classic 320×200 bitmap interface | Essentially complete |
| **Classic Extended** | Classic data, optionally extended where the classic screens can show it | Working idea: Classic engine plus features **backported from Modern**, step by step (§4) | Classic bitmap interface, extended by hand per feature | Open; after Modern |
| **Modern** | New OBW data set (imported and extended, or own worlds) | Modern: generated calendar, data-driven leagues and competitions, playable foreign leagues, leagues larger than 12 teams | New high-resolution interface (RmlUi), designed for many leagues | Interface in progress |

All three use **the same match engine**. A match between two given teams under the same rules is simulated by the same code in every variant; only what happens around it (calendar, competitions, finances, transfers, development) differs.

### 1.1 Where a change belongs

- **Classic** receives changes that work with the imported original assets and the original data layout: bug fixes, quality of life, small rule corrections, menus such as the environment menu of a later DIAMOND release.
- **Modern** is where new features are designed first, without the limits of the original screens or data layout: more leagues, foreign leagues with more than 12 teams, a free calendar, new competitions and systems.
- **Classic Extended** takes Modern features back into the Classic engine **one at a time**, where the bitmap interface can present them with reasonable manual work. The classic screens were never designed for variable structures, so Classic Extended will never offer every Modern feature; each backport is a decision of its own (§4).

## 2. Target architecture

```
      Classic UI (bitmap)         Classic UI + extra screens        Modern UI (RmlUi)
              |                              |                              |
              +---------------- app / protocol layer (capabilities) -------+
              |                              |                              |
      season: classic          season: classic + backported features   season: modern
      (GameState image)        (GameState image + extension state)    (typed OBW model)
              |                              |   \                    /     |
              |                              |    shared feature modules    |
              |                              |    (bmh_features, ports)     |
              +------------------ match adapter (per engine) --------------+
                                             |
                              match engine (bmh_match: typed contract)
                                             |
                              random sources, attribute set bmh-classic@1
```

### 2.1 Libraries

| Target | Contents | May depend on |
|---|---|---|
| `bmh_match` | Minute simulation, events, chances, cards, injuries during the match, substitutions, live interventions, the Reality scene and reporter | `random`, the match contract types; **not** `GameState`, calendar, finances |
| `bmh_season_classic` | Today's `src/game` without the match core: turn driver, weekly/monthly/season processing, cups, transfers, finances, post-match effects | `bmh_match`, `bmhcore` formats |
| `bmh_season_classic_ext` | Classic Extended: registry of backported features, their state and hook points in the Classic turn (§4) | `bmh_season_classic`, `bmh_features` |
| `bmh_features` | Season features written once for Modern and reusable by Classic Extended, each against small **ports** (interfaces) instead of a concrete state model | the match contract types only |
| `bmh_season_modern` | New season engine on the typed OBW model (§5) | `bmh_match`, `bmh_features`, `bmh_world` |
| `bmh_world` | Typed OBW model, reader/writer, validator (`src/content`, `src/import`) | `bmhcore` formats |

The dependency rules: **the match engine knows no season engine**; no season engine reaches into the state of another; **no season engine knows an interface**; a feature in `bmh_features` knows neither `GameState` nor the OBW model, only its ports.

### 2.2 Writing Modern features so they can be backported

Backporting is cheap only if a feature is written for it from the start:

- The rules of a feature (for example youth development, a staff system, sponsor negotiation) live in `bmh_features` and read and write the game only through a port: `PlayerRoster`, `ClubFinance`, `Calendar`, `Mailbox`, `RandomSources` and similar narrow interfaces.
- Modern implements the ports on the typed model. Classic Extended implements the same ports as adapters on `GameState` plus its extension state.
- A feature declares what it needs (ports, data fields, calendar events) and which presentation it needs (screens, messages). This declaration is the checklist for a backport: if the Classic adapter cannot provide a port, or no classic screen can show the result, the feature stays Modern-only.
- Features that depend on Modern-only structures (generated calendar, generic competition stages, leagues of variable size) are marked as such and are not candidates.

## 3. The match contract

Today the match core reads and writes the memory image directly: `MatchGroup` holds raw descriptors, and `matchday.cpp` mixes the minute loop with season effects (`finish_match`, `update_club_table`, `update_morale`, `develop_players`, `book_money`, `injure_player`, `rate_manager`). The split replaces this with typed input and output.

### 3.1 Input: `MatchdaySetup`

The contract works per **matchday batch**, not per single match: the original walks all match groups and all matches every minute, so the random numbers of different matches are interleaved. A batch keeps that order exactly for Classic; Modern may pass a batch of one.

```
MatchdaySetup
  rules         per group: competition kind (league, cup, friendly, indoor),
                half length (45 / 20), stoppage, extra time, penalties,
                away-goals and away-penalty flags, leg and first-leg score
  groups[]      matches[]: home TeamSheet, away TeamSheet, venue, spectators
  random        RandomSources (see 3.3)
  live          optional LiveControl (see 3.4)

TeamSheet
  team id, side, players on the pitch and bench (player id, position, slot,
  attributes of set bmh-classic@1, condition, form, morale, cards already held),
  formation, tactic items, attitude, captain, penalty and free-kick takers,
  automation flags (computer-controlled tactics)
```

Everything a match needs is copied into the setup. The engine does not look anything up.

### 3.2 Output: `MatchdayResult`

```
per match: final score per phase (regular, extra time, penalties), end minute,
           event list (minute, kind, team, players: goal, assist, chance,
           card, injury with severity, substitution, tactical change),
           player ratings and statistics, side records (typed form of the
           18 legacy record bytes), live-intervention log
```

The engine reports **facts**. Consequences belong to the season engine: league tables, money, morale, player development, suspensions counted per competition, injury duration in days, manager ratings, mail and press. Classic applies them exactly as today; Modern applies its own rules to the same facts.

### 3.3 Random numbers

The match engine draws only through an injected `RandomSources` interface with the two streams the original uses (`WatcomRandom`, `GameRandom`, see `docs/RNG.md`).

- Classic passes the session's global streams, so consumption order and regression results stay identical.
- Modern passes streams seeded per matchday (or per match) from the world seed, so a match can be replayed on its own.

### 3.4 Live matches

`LiveControl` is the only channel for human influence during a match: pause points (half time, events that request a lineup), substitutions, tactic and attitude changes, and the minute clock barrier of multi-client sessions. The Reality scene (`reality_*`) is a presentation of the same simulation and moves with the match engine. Interfaces receive match progress as events; none of them calls into the minute loop directly.

### 3.5 Attribute set

The match engine consumes players in attribute set `bmh-classic@1` (OBW §5.3). Classic already stores exactly this. Modern may keep richer attributes for its own systems, but provides a `bmh-classic@1` projection for every player it sends into a match. A later, finer match model would be a **new version** of the match engine with its own attribute set, selectable per variant; it is not part of this plan.

### 3.6 Versioning

`bmh_match` carries a contract version (`match@1`). Saves record the version they were played with. A behavioural change to the match engine is a new version, noted in `docs/DIFFERENCES.md`, and is checked against all three variants before it lands.

## 4. Classic Extended

> **Working idea, not a decision.** Backporting Modern features into the Classic engine is the current idea for Classic Extended; the variant may take a different shape. What does not depend on it: the shared match engine (§3), the Modern engine driven only through the protocol, and features written against ports (§2.2), which keep several options open.

Classic Extended is the **Classic season engine plus backported Modern features**, presented in the classic bitmap interface.

- **Invariant:** with every backported feature switched off, Classic Extended produces exactly the Classic results. A test runs the Classic regression through the Extended engine with no features.
- Each backported feature is a module with an id and version, its own state (`x-extended/` in OBW saves), the Classic adapters for its ports and its classic-style screens. A career records which features it uses; features can be switched on at career start and, where safe, later.
- Features hook into defined points of the Classic turn (start/end of day, week, month and season; before/after a matchday; desk actions), never into the middle of a Classic routine.
- **Data:** Classic Extended starts from Classic data. A feature may bring additional data, but only in a shape the classic screens can show. Structures that change the size of what the screens list (more or larger leagues, new competitions) are Modern-only unless a specific screen is redesigned for them.
- **Interface cost per feature**, the main factor in each backport decision:
  1. The result fits existing screens (a new value on the player card, a message, a desk action): small.
  2. A new screen of fixed size in the classic style (a staff or youth screen): medium, new own artwork needed.
  3. A screen whose content grows with the data (longer tables, more league selectors, calendar views): large, often not worth it.
- **Assets:** the original bitmaps of the user's installation plus new classic-style artwork. New artwork must be the project's own.
- Order of work: a feature is built and settled in Modern first, then backported if its ports and its presentation fit (§2.2).

## 5. Modern

> **Decision for 0.3 (October 2026):** Modern starts as a **fork of the Classic season core** in `src/modern/`, sharing formats, `GameState` (with a world layout) and the match core with Classic. It runs only on Modern data sets and moves away from Classic step by step; Classic stays unchanged. The typed engine described below remains the direction, reached gradually rather than by a rewrite. The 0.3 plan, status and UI interface are documented in German under [new_engine](new_engine/README.md).

Modern is a new season engine on the typed OBW model (`docs/formats/OBW.md`):

- **Data set:** an OBW world that goes beyond the original structure, for example foreign leagues with more than 12 teams, more leagues (up to 32) and real-world competition formats. It can start from an imported Classic world and be extended, or be a world of its own.
- **Calendar:** generated each season from `world/calendar.json` (series, windows, events) instead of a fixed day table; any season start, international windows, winter breaks per nation.
- **Competitions:** the generic stage model (league, groups, league phase, knockout, playoff, final) with declarative slots and promotion/relegation; foreign leagues are playable, not only simulated.
- **Systems:** finances, transfers, contracts, youth, staff and development are rewritten on the typed model, reusing Classic formulas where they fit and documenting where they differ.
- **Interface:** the RmlUi client through the app/protocol layer only.
- Matches always go through the shared match contract (§3).

Modern does not have to reproduce Classic results; its quality gate is plausibility (seasons simulated in bulk, agentic QA, fan expectations), not bit equality.

## 6. Variant selection and saves

- The world or save manifest names the runtime: `requires: ["runtime:classic-v1"]`, `["runtime:classic-extended-v1"]` (with the list of active features and their versions) or `["runtime:modern-v1"]`, plus `match@1`.
- `CareerSession` selects the season engine from the manifest; the protocol reports **capabilities** per engine and feature, so interfaces and console clients only offer supported actions.
- Conversions are **one-way**: Classic → Classic Extended (switch features on), Classic or Classic Extended → Modern (convert to the typed model; backported feature state maps to the Modern original of the feature). There is no way back, and the original file is kept.
- **All variants save only in the OBW format** (`.obs`). Legacy `.FBM` saves are an import source only: they are converted into a Classic `.obs` career and never written.

## 7. Migration plan

Each step keeps the game playable and the Classic regression (`tools/regression/checkonly.sh`, season simulations, long runs) unchanged.

| Step | Content | Gate |
|---|---|---|
| 0 | Freeze the Classic baseline: tag, record regression outputs and long-run statistics as reference | Reference recorded |
| 1 | Move the match core (`match_engine`, `match`, `reality_*`, reporter) into `src/match` and target `bmh_match`; no behaviour change | Regression identical |
| 2 | Introduce `RandomSources`; the match core no longer touches the global streams directly | Regression identical |
| 3 | Typed `MatchdaySetup`/`MatchdayResult`; Classic adapter builds the setup from `MatchGroup`/`GameState` and writes results back; move post-match effects out of the minute loop into `bmh_season_classic` | Regression identical; match contract tests |
| 4 | Remove the remaining `GameState` dependencies from `bmh_match` (enforced by the build: `bmh_match` does not link `GameState`) | Build rule; regression identical |
| 5 | Runtime field in manifests, engine selection in `CareerSession`, capability reporting in the protocol | Classic unchanged; capability tests |
| 6 | `bmh_features` with the first ports; Modern season engine on `bmh_world`, first with one nation and the shared match engine, then the competition model, then foreign leagues and larger leagues | Modern plausibility runs |
| 7 | Classic Extended skeleton: feature registry, hook points, `x-extended` state, Classic adapters for the first ports, "no features = Classic" test | Regression identical through Extended |
| 8 | Backports one feature at a time, each with its interface cost assessed (§4) | Per feature: Classic regression with the feature off, plausibility with it on |
| 9 | Classic → Modern conversion of worlds and careers | Converted careers playable |

Steps 1 to 4 are the actual split and the risky part; they are pure refactoring and must be done in small commits with the regression after each. Classic improvements continue in parallel on `bmh_season_classic`.

For 0.3 the order changes: the Modern fork (phases M0–M7 in [new_engine](new_engine/phasen.md)) comes first and shares the match core as it is; steps 1 to 4 follow later and then serve both season cores.

## 8. Tests

- **Match contract tests:** a fixed `MatchdaySetup` with fixed seeds yields a fixed `MatchdayResult` (golden files with synthetic teams, public).
- **Classic regression:** unchanged scripts and expected outputs; run after every step.
- **Extended identity:** the Classic regression through the Extended engine with no features.
- **Feature tests:** each module in `bmh_features` is tested once against fake ports, then through both adapters (Modern and Classic Extended).
- **Modern plausibility:** bulk season simulations with goal, points and money distributions compared to Classic as a reference band.
- **Dependency checks:** the build fails if `bmh_match` includes season or `GameState` headers, if `bmh_features` includes `GameState` or OBW model headers, or if a season engine includes interface headers.

## 9. Open decisions

- Which features become the first backport candidates (proposal: features of interface cost 1 or 2 that do not change league structure).
- Whether Modern keeps `bmh-classic@1` as its only player attribute set at first (proposal: yes, richer attributes later as a new match engine version).
- Ownership: the split and Classic (engine/integration), the Modern interface (UI), Modern features and their backports (shared, per feature).
