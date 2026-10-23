<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Editor 0.5.2: read-only regulations

Native 1440×900 SDL2/OpenGL/RmlUI captures on Linux with software OpenGL. All
records and artwork are synthetic. No installation, real club/player data,
savegame, private archive or user profile is included.

| View | Capture |
|---|---|
| Before: competition inspector, released 0.5.1 | [Before](before-competitions-0.5.1.png) |
| Promotion/relegation, Midnight Glass / German | [After](rules-midnight-de.png) |
| International qualification and replacement | [Qualification](qualification-midnight-de.png) |
| Preserved game rule and attribute ranges | [Game rules](game-rules-midnight-de.png) |
| Silver Glass / English | [Silver](rules-silver-en.png) |
| Matchday / German | [Matchday](rules-matchday-de.png) |

Create the display fixture using the editor's own synthetic demo:

```sh
python3 docs/qa/editor-regulations/make_fixture.py PATH/TO/OpenBMHEditor TEMP/synthetic.obw
OpenBMHEditor --open TEMP/synthetic.obw --page regulations
```

The recipe adds synthetic upper/lower leagues, explicit rank ranges, a playoff,
an international qualification selector and fallback. It is a display fixture,
not a playable season. Options selects the language/theme. Scroll the right
panel to qualification; select **Game rules** and `demo@1` to inspect the stored
attribute ranges. Before was captured with the same fixture in the 0.5.1
competition inspector, which had no dedicated rule view.

Validation: existing editor core/MCP tests, comparisons, version and new
regulations tests; native mouse/keyboard integration and package-boundary
checks. The regulations tests cover declared movement, both slot locations,
explicit destinations, fallback preservation, language/filtering, parallel
Classic champions/playoffs, the bottom newcomer pool, foreign closed leagues,
the named UEFA quotas, declared-rule precedence, null extensions and unchanged
revision/history. The GUI test switches both rule tabs, searches and verifies
the shared read-only MCP projection without changing the world.

Actual Diamond source identity, unchanged game data, images, round-trip checks
and captures are recorded separately in the private content repository.
