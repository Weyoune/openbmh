# Editor 0.5.1 — ratings and salaries

These are actual 1440×900 SDL2/OpenGL/RmlUI captures on Linux under Xvfb
with software OpenGL. All content is the synthetic `World::demo()` fixture:
52 invented players, two invented clubs, one league and 50 explicitly synthetic
per-season contracts (one unknown salary per club). No real-world data, original
game images, personal paths, settings or credentials are included. This editor
uses starting worlds, not a career save; reproduce the data with `--demo`.

| View | Evidence |
|---|---|
| Before, released Editor 0.5.0 | [Player inspector](before-players-0.5.0.png), without a ratings/payroll overview |
| Players, Midnight Glass / German | [Salary descending](players-midnight-de.png) |
| Teams, Silver Glass / English | [Player-weighted means, payroll and coverage](teams-silver-en.png) |
| Leagues, Matchday / German | [League aggregate](leagues-matchday-de.png) |
| Attribute selection | [Choose columns](columns-midnight-de.png) |
| Horizontal navigation | [Further attributes](players-scroll-midnight-de.png); header and values remain aligned |

The after views use Editor 0.5.1. The before capture uses the released 0.5.0
Linux executable and its own assets. No screenshot is a browser mockup.

Validation:

- `editor_core_tests`, `editor_comparison_tests`, `editor_version` pass locally.
  The comparison suite checks 36 outcomes, including actual currency units,
  unknown versus zero salaries, date/team selection, conflicting contracts,
  staff exclusion, complete reserves, duplicate evidence, player-weighted means,
  coverage, empty groups, numeric sorting across more than 200 rows, stable ties,
  nulls last in both directions, different rating systems, overflow, read-only
  session state, cache refresh after editing/undo and the shared MCP tool.
- `tests/editor_gui_smoke.py` performs real keyboard/mouse operations. It selects
  a rating column via its label, sorts ratings and salaries, opens the first row
  and edits it. MCP verifies the expected stable player/club/league identifier
  changed, then undoes it. The existing inspector/search/JSON/undo/save checks
  still pass. The comparison leaves the saved world clean.
- All three themes and both languages were inspected. Header arrows do not rely
  on missing font glyphs; horizontal scrolling keeps column alignment.
- The completed private season was checked independently: 18,920 eligible
  player rows, 545 teams, 32 leagues; 832 source-only duplicate rows excluded.
  Every eligible player has one known current-team salary in that package.
  These are dataset estimates, not verified real wages. No private rows or
  screenshots are distributed in this public directory.

Build the targets and run the CTest commands in [the editor guide](../../EDITOR.md).
For the native input test, run `python3 tests/editor_gui_smoke.py <OpenBMHEditor>`
inside a graphical session or Xvfb with xdotool. `OpenBMHEditor --demo --page
comparison` opens the overview; the three buttons choose players, teams or
leagues. **Choose columns** selects additional attributes.

Windows/macOS native build and core tests run in the editor CI. The Linux
window checks do not establish human acceptance on those operating systems.
