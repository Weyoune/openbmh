# 1.2 game scope

The setup screen has four independent Original controls: **Deutschland**, **England**, **International**, **Laden**. Germany and England toggle their club systems. International retains the Original globe's national-team/tournament meaning; it is not the European club-cup switch. Load does not alter scope.

At least one scope must remain enabled. International-only selection retains the Original single-manager restriction; a multiplayer setup must retain at least one club system.

| Club selection | Active domestic ladders | European AI leagues | League clubs |
| --- | --- | --- | --- |
| Germany + England | Both | Spain, France, Italy, Portugal | 252 |
| Germany only | Germany | Spain, France, Italy, Portugal | 162 |
| England only | England | Spain, France, Italy, Portugal | 162 |
| International only | None | None | 0 |

International may additionally be enabled or disabled in each club-career combination. European club competitions remain part of club play.

## Actual removal, not a display filter

`select_world_scope` creates a fresh career world from the complete immutable master before manager selection. An excluded German/English club is removed from the club map, including amateur/non-league fallback entries. It is not moved into Europe or retained as an AI opponent. The corresponding league memberships, result storage, domestic cup setup and calendar appointments are absent. Active table selectors and manager-country choices omit that country.

Surviving club/player/crest IDs remain unchanged. Players belonging exclusively to removed squads are removed from club-career data rather than silently becoming free agents. Nationality is not a filter: a German player at a retained English/other European club remains. National-only play preserves the original independent nationality/player pool and has no club transfer market. National team records follow the International switch, so disabling a country's domestic clubs does not remove that nation's team from an enabled national tournament.

The complete source package retains both countries so a later *new* game can choose a different combination. A running career cannot be re-scoped; selection rejects active managers/loans and already filtered source worlds. Saved worlds encode the actual selected entities, not an activation mask over a hidden second league system.

## Stable schema and progression

League IDs retain their fourteen reserved catalogue slots. Disabled slots have no members, rounds or results; they are metadata identities, not live leagues or substitute club lists. Database schema versions 2 and 3 store strict Germany/England/International scope booleans; version 3 adds persistent match-state fields. Version-1 full master worlds remain readable as the default full scope. Scoped world/save roundtrips retain the selection.

Calendar import drops disabled domestic appointments. Shared European club events require club play, while national tournaments/friendlies require International. Calendar/save validation rejects disabled-country events reintroduced by malformed data. Initial cup selection does not read or consume randomness for an excluded country.

## UI boundary and QA

Private `KB/SETUP12.VGA` now contains four columns and two rows of the complete supplied Original controls, including their original crossed-out states and the load disk. No country silhouettes or flag textures are repainted. The UI preview uses `--league-symbol countries` (`dual` remains a preview-command alias).

Tests cover all seven valid selections, complete league-only coordinator years, absence of domestic/amateur fallback clubs, stable IDs, conditional cup initialization, national/European calendar filtering and scoped codec/save roundtrips. These are test-score coordinator runs, not played careers.

The new setup emits a typed-country career request before manager selection. The legacy German/English single-edition manager/session adapter rejects that request explicitly; it cannot silently start the wrong country or a hidden combined world. The complete typed manager/GUI-career and national-team simulation adapters remain pending, so this selection/data work is not a playable 1.2 release.
