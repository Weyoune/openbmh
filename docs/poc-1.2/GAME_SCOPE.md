# 1.2 game scope

The setup screen has four independent Original controls: **Deutschland**, **England**, **International**, **Laden**. Germany and England toggle their club systems. International retains the Original globe's national-team/tournament meaning; it is not the European club-cup switch. Load does not alter scope.

At least one scope must remain enabled. International-only selection retains the Original single-manager restriction; a multiplayer setup must retain at least one club system.

| Club selection | Active domestic ladders | European AI leagues | League clubs |
| --- | --- | --- | --- |
| Germany + England | Both | Spain, France, Italy | 234 |
| Germany only | Germany | Spain, Premier League, France, Italy | 164 |
| England only | England | Spain, France, Italy | 144 |
| International only | None | None | 0 |

International may additionally be enabled or disabled in each club-career combination. European club competitions remain part of club play.

## Actual removal, not a display filter

`select_world_scope` creates a fresh career world from the complete immutable master before manager selection. Disabled Germany is removed completely, including its amateur/non-league entries. Disabled England retains **only the twenty Premier League clubs** as AI opposition. Its four lower divisions, amateur pool, domestic cups and Charity Shield are absent; no human manager can select an English club. The AI-only Premier League plays 38 rounds without domestic relegation.

The Premier League always uses league ID 5 and the same club/player IDs. With England enabled it belongs exclusively to the English table group; otherwise it occupies the original English international-table slot, in the order Spain, England, France, Italy. No duplicate league or Portuguese league is created. Original Portuguese cup-only opponents remain independent of this league choice.

Surviving club/player/crest IDs remain unchanged. Players belonging exclusively to removed squads are removed from club-career data rather than silently becoming free agents. Nationality is not a filter: a German player at a retained English/other European club remains. National-only play preserves the original independent nationality/player pool and has no club transfer market. National team records follow the International switch, so disabling a country's domestic clubs does not remove that nation's team from an enabled national tournament.

The complete source package retains both countries so a later *new* game can choose a different combination. A running career cannot be re-scoped; selection rejects active managers/loans and already filtered source worlds. Saved worlds encode the actual selected entities, not an activation mask over a hidden second league system.

## Stable schema and progression

League IDs retain their thirteen reserved catalogue slots (IDs 0–12). Disabled slots have no members, rounds or results; they are metadata identities, not live leagues or substitute club lists. Database schema versions 2–6 store strict Germany/England/International scope booleans; versions 3/4 add persistent match/development/club-record fields and version 5 adds optional manager finances and version 6 adds optional verified tactical fields. Profile `dual-classic-2` is required; the retired unreleased Portugal profile is rejected. Snapshot magic is `OBMH12S4`, rejecting older development layouts explicitly. Version-1 full master worlds with the current profile remain readable as the default full scope. Scoped world/save roundtrips retain the selection. Developed worlds and played club records cannot be re-scoped as fresh master data.

Calendar import drops disabled domestic appointments. Shared European club events require club play, while national tournaments/friendlies require International. Calendar/save validation rejects disabled-country events reintroduced by malformed data. Initial cup selection does not read or consume randomness for an excluded country.

## UI boundary and QA

Private `KB/SETUP12.VGA` now contains four columns and two rows of the complete supplied Original controls, including their original crossed-out states and the load disk. No country silhouettes or flag textures are repainted. The UI preview uses `--league-symbol countries` (`dual` remains a preview-command alias).

Tests cover all seven valid selections, complete league-only coordinator years, absence of disabled lower/amateur clubs and retention of the AI-only Premier League, stable IDs, conditional cup initialization, national/European calendar filtering and scoped codec/save roundtrips. These are test-score coordinator runs, not played careers.

The new setup emits a typed-country career request before manager selection. The legacy German/English single-edition manager/session adapter rejects that request explicitly; it cannot silently start the wrong country or a hidden combined world. The complete typed manager/GUI-career and national-team simulation adapters remain pending, so this selection/data work is not a playable 1.2 release.
