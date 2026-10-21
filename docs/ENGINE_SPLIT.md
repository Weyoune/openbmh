# Engine split: Classic, Classic Extended and Modern

Status: **concept and migration plan**. It prepares one shared match engine under three season engines. Nothing in this document changes the behaviour of the current release.

## 1. Goal

OpenBMH will offer three game variants:

| Variant | Data | Season engine | Interface | State |
|---|---|---|---|---|
| **Classic** | Imported from the user's BMH or FLTD installation (OBW `runtime:classic-v1`) | The current engine: fixed calendar, 9 leagues, original competition structure and rules | Classic 320×200 interface | Essentially complete |
| **Classic Extended** | Classic data plus extension data | Classic engine with optional, switchable extensions | Classic interface plus additional screens in the same style | Not started |
| **Modern** | Typed OBW worlds (imported or own) | New engine: generated calendar, data-driven leagues and competitions, playable foreign leagues | New high-resolution interface (RmlUi) | Interface in progress |

All three use **the same match engine**. A match between two given teams under the same rules is simulated by the same code in every variant; only what happens around it (calendar, competitions, finances, transfers, development) differs.

## 2. Target architecture

```
            Classic UI     Classic Extended UI      Modern UI (RmlUi)
                 \                 |                    /
                  +-------- app / protocol layer -------+     variant capabilities
                 /                 |                    \
   season: classic    season: classic + extensions    season: modern
   (GameState image)  (GameState image + ext state)   (typed OBW model)
                 \                 |                    /
                  +------ match adapter (per engine) ---+
                                   |
                     match engine  (bmh_match: typed contract)
                                   |
                     random sources, attribute set bmh-classic@1
```

### 2.1 Libraries

| Target | Contents | May depend on |
|---|---|---|
| `bmh_match` | Minute simulation, events, chances, cards, injuries during the match, substitutions, live interventions, the Reality scene and reporter | `random`, the match contract types; **not** `GameState`, calendar, finances |
| `bmh_season_classic` | Today's `src/game` without the match core: turn driver, weekly/monthly/season processing, cups, transfers, finances, post-match effects | `bmh_match`, `bmhcore` formats |
| `bmh_season_classic_ext` | Extension modules on top of Classic (§4) | `bmh_season_classic` |
| `bmh_season_modern` | New season engine on the typed OBW model (§5) | `bmh_match`, `bmh_world` |
| `bmh_world` | Typed OBW model, reader/writer, validator, importers (`src/import`) | `bmhcore` formats |

The dependency rule: **the match engine knows no season engine**, and no season engine reaches into another one's state.

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

The match engine consumes players in attribute set `bmh-classic@1` (OBW §5.3). Classic and Classic Extended already store exactly this. Modern may keep richer attributes for its own systems, but provides a `bmh-classic@1` projection for every player it sends into a match. A later, finer match model would be a **new version** of the match engine with its own attribute set, selectable per variant; it is not part of this plan.

### 3.6 Versioning

`bmh_match` carries a contract version (`match@1`). Saves record the version they were played with. A behavioural change to the match engine is a new version, noted in `docs/DIFFERENCES.md`, and is checked against all three variants before it lands.

## 4. Classic Extended

Classic Extended is **Classic plus switchable extensions**:

- It runs on the Classic season engine and the Classic save layout, plus one additional state block for extensions (`x-extended/` in OBW saves).
- Each extension is a module with an id, a version, its own state, its rules and its screens. A career records which extensions it uses.
- **Invariant:** with every extension switched off, Classic Extended produces exactly the Classic results. A test runs the Classic regression through the Extended engine with no extensions.
- Extensions hook into defined points of the Classic turn (start/end of day, week, month, season; before/after matchday; desk actions), never into the middle of a Classic routine.
- Its screens use the classic interface toolkit (`src/ui`), in the same resolution and style.
- Candidates for first extensions: the environment ("Umfeld") menu of a later DIAMOND release (reviewed against our rule base, documented as a deviation), more statistics and history screens, quality-of-life automation, configurable rule values (roadmap 0.3).

## 5. Modern

Modern is a new season engine on the typed OBW model (`docs/formats/OBW.md`):

- **Calendar:** generated each season from `world/calendar.json` (series, windows, events) instead of a fixed day table; any season start, international windows, winter breaks per nation.
- **Competitions:** the generic stage model (league, groups, league phase, knockout, playoff, final) with declarative slots and promotion/relegation; up to 32 leagues; foreign leagues are playable, not only simulated.
- **Systems:** finances, transfers, contracts, youth, staff and development are rewritten on the typed model, reusing Classic formulas where they fit, documented where they differ.
- **Interface:** the RmlUi client through the app/protocol layer only.
- Matches always go through the shared match contract (§3).

Modern does not have to reproduce Classic results; its quality gate is plausibility (seasons simulated in bulk, agentic QA, fan expectations), not bit equality.

## 6. Variant selection and saves

- The world or save manifest names the runtime: `requires: ["runtime:classic-v1"]`, `["runtime:classic-extended-v1"]` or `["runtime:modern-v1"]`, plus `match@1`.
- `CareerSession` selects the season engine from the manifest; the protocol reports **capabilities** per variant, so interfaces and console clients only offer supported actions.
- Conversions are **one-way**: Classic → Classic Extended (switch on extensions), Classic or Classic Extended → Modern (convert to the typed model). There is no way back, and the original file is kept.
- Legacy `.FBM` saves stay Classic.

## 7. Migration plan

Each step keeps the game playable and the Classic regression (`tools/regression/checkonly.sh`, season simulations, long runs) unchanged.

| Step | Content | Gate |
|---|---|---|
| 0 | Freeze the Classic baseline: tag, record regression outputs and long-run statistics as reference | Reference recorded |
| 1 | Move the match core (`match_engine`, `match`, `reality_*`, reporter) into `src/match` and target `bmh_match`; no behaviour change | Regression identical |
| 2 | Introduce `RandomSources`; the match core no longer touches the global streams directly | Regression identical |
| 3 | Typed `MatchdaySetup`/`MatchdayResult`; Classic adapter builds the setup from `MatchGroup`/`GameState` and writes results back; move post-match effects out of the minute loop into `bmh_season_classic` | Regression identical; match contract tests |
| 4 | Remove the remaining `GameState` dependencies from `bmh_match` (enforced by the build: `bmh_match` does not link `GameState`) | Build rule; regression identical |
| 5 | Variant field in manifests, engine selection in `CareerSession`, capability reporting in the protocol | Classic unchanged; capability tests |
| 6 | Classic Extended skeleton: extension registry, hook points, `x-extended` state, "all extensions off = Classic" test | Regression identical through Extended |
| 7 | Modern season engine on `bmh_world`, first with one nation and the shared match engine, then the competition model, then foreign leagues | Modern plausibility runs |
| 8 | First Classic Extended extensions; Modern feature growth | Per extension/feature |

Steps 1 to 4 are the actual split and the risky part; they are pure refactoring and must be done in small commits with the regression after each.

## 8. Tests

- **Match contract tests:** a fixed `MatchdaySetup` with fixed seeds yields a fixed `MatchdayResult` (golden files with synthetic teams, public).
- **Classic regression:** unchanged scripts and expected outputs; run after every step.
- **Extended identity:** Classic regression through the Extended engine with no extensions.
- **Modern plausibility:** bulk season simulations with goal, points and money distributions compared to Classic as a reference band.
- **Dependency check:** the build fails if `bmh_match` includes season or `GameState` headers.

## 9. Open decisions

- Whether Classic Extended may change the save layout for its extensions only in OBW saves or also offer a legacy `.FBM` export (proposal: OBW only).
- Whether Modern keeps `bmh-classic@1` as its only player attribute set at first (proposal: yes, richer attributes later).
- Which team owns which variant: Classic and the split (engine/integration), Modern interface (UI), Classic Extended extensions (both, per module).
