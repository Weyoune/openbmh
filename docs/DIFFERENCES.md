# Intentional differences from the original

Generated from source comment markers by `tools/public_release/collect_differences.py`.

See [Compatibility decisions](COMPATIBILITY.md) for the overall policy. These entries
describe deliberate changes; they are not a list of every known compatibility limit.

- [src/app/career_session.cpp](../src/app/career_session.cpp): supports designated set-piece record initialization and validation.
- [src/app/career_session.cpp](../src/app/career_session.cpp): native saves do not consume gameplay randomness for encryption.
- [src/app/quit_confirmation.hpp](../src/app/quit_confirmation.hpp): confirms application exit with a localized native dialog, with cancellation selected by default.
- [src/formats/le.hpp](../src/formats/le.hpp): applies a shared supported rule profile when loading editions with different rule-table values.
- [src/formats/rule_base.cpp](../src/formats/rule_base.cpp): normalizes live-scene defaults, league strength ranges, option defaults, stadium prices and capacities, betting limits, and the foreign-player limit across supported editions.
- [src/formats/rule_base.hpp](../src/formats/rule_base.hpp): normalizes live-scene options, league strength ranges, option defaults, stadium prices and capacities, betting limits, and the foreign-player limit.
- [src/game/career.cpp](../src/game/career.cpp): skips reserved calendar positions when assigning a joining manager to a club.
- [src/game/career.hpp](../src/game/career.hpp): uses the supported player-generation ranges and excludes reserved calendar positions when assigning managers.
- [src/game/desk_actions.cpp](../src/game/desk_actions.cpp): uses the supported transfer-fee arithmetic and designated set-piece fields.
- [src/game/desk_actions.hpp](../src/game/desk_actions.hpp): supports designated set-piece records and the supported transfer-editor rules.
- [src/game/environment.hpp](../src/game/environment.hpp): uses six monthly environment budgets across data editions while retaining the shared staff prices and academy player-generation strength bands.
- [src/game/highscore.cpp](../src/game/highscore.cpp): reads the supported edition-specific high-score format from the loaded image.
- [src/game/lineup.cpp](../src/game/lineup.cpp): disables the foreign-player limit in the supported rule profile.
- [src/game/lineup.hpp](../src/game/lineup.hpp): disables the foreign-player limit and supports owner-bound designated set-piece selection.
- [src/game/match.hpp](../src/game/match.hpp): uses the enhanced injury selection and suspension rules.
- [src/game/match_engine.cpp](../src/game/match_engine.cpp): supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
- [src/game/season.cpp](../src/game/season.cpp): retains the enhanced academy player-generation strength bands with monthly youth budgets.
- [src/game/season.hpp](../src/game/season.hpp): retains the enhanced academy player-generation strength bands with monthly youth budgets.
- [src/game/state.hpp](../src/game/state.hpp): retains designated set-piece selections in the supported manager-record extension.
- [src/game/week.cpp](../src/game/week.cpp): uses the enhanced training rules: focus strength is at least five below its target, and completed skills rotate to another focus.
- [src/game/week.hpp](../src/game/week.hpp): uses the enhanced assistant-training rules, retaining the selected skill until it reaches its maximum.
- [src/game/weekly.cpp](../src/game/weekly.cpp): uses the supported weekly-event configuration, with its rule switch disabled.
- [src/game/weekly.hpp](../src/game/weekly.hpp): uses the supported weekly processing and transfer rule profile.
- [src/modern/app/modern_session.cpp](../src/modern/app/modern_session.cpp): supports designated set-piece record initialization and validation.
- [src/modern/app/modern_session.cpp](../src/modern/app/modern_session.cpp): native saves do not consume gameplay randomness for encryption.
- [src/modern/content/world_expand.cpp](../src/modern/content/world_expand.cpp): Modern worlds have league sizes, clubs and players of their own; players whose nation has no single-byte nationality code carry a substitute foreign code.
- [src/modern/game/career.cpp](../src/modern/game/career.cpp): in a Modern world a club's place in the strength band of its league follows the data set's ranking of the squads instead of the table place, and its players keep their relative values (balance.md).
- [src/modern/game/career.cpp](../src/modern/game/career.cpp): skips reserved calendar positions when assigning a joining manager to a club.
- [src/modern/game/career.hpp](../src/modern/game/career.hpp): uses the supported player-generation ranges and excludes reserved calendar positions when assigning managers.
- [src/modern/game/desk_actions.cpp](../src/modern/game/desk_actions.cpp): uses the supported transfer-fee arithmetic and designated set-piece fields.
- [src/modern/game/desk_actions.cpp](../src/modern/game/desk_actions.cpp): Modern training camps have grades up to six stars with a capped step, a higher limit and recovery extras.
- [src/modern/game/desk_actions.hpp](../src/modern/game/desk_actions.hpp): supports designated set-piece records and the supported transfer-editor rules.
- [src/modern/game/environment.hpp](../src/modern/game/environment.hpp): uses six monthly environment budgets across data editions while retaining the shared staff prices and academy player-generation strength bands.
- [src/modern/game/highscore.cpp](../src/modern/game/highscore.cpp): reads the supported edition-specific high-score format from the loaded image.
- [src/modern/game/matchday_setup.cpp](../src/modern/game/matchday_setup.cpp): in Modern worlds a referee may take a second match on a day with more matches than referees.
- [src/modern/game/season.cpp](../src/modern/game/season.cpp): retains the enhanced academy player-generation strength bands with monthly youth budgets.
- [src/modern/game/season.hpp](../src/modern/game/season.hpp): retains the enhanced academy player-generation strength bands with monthly youth budgets.
- [src/modern/game/week.cpp](../src/modern/game/week.cpp): uses the enhanced training rules: focus strength is at least five below its target, and completed skills rotate to another focus.
- [src/modern/game/week.hpp](../src/modern/game/week.hpp): uses the enhanced assistant-training rules, retaining the selected skill until it reaches its maximum.
- [src/modern/game/weekly.cpp](../src/modern/game/weekly.cpp): uses the supported weekly-event configuration, with its rule switch disabled.
- [src/modern/game/weekly.hpp](../src/modern/game/weekly.hpp): uses the supported weekly processing and transfer rule profile.
- [src/modern/ui/demo_calendar.cpp](../src/modern/ui/demo_calendar.cpp): the native season follows its saved start year (including leap days), rather than the HTML's fixed 2027/28.
- [src/ui/league_table.hpp](../src/ui/league_table.hpp): uses three points for a win in the supported rule profile.
- [src/ui/pitch.cpp](../src/ui/pitch.cpp): supports owner-bound designated set-piece selection in the extended manager record.
- [src/ui/save_load.cpp](../src/ui/save_load.cpp): focuses the host save filename on entry and selects an existing name for replacement.
- [src/ui/save_load.cpp](../src/ui/save_load.cpp): omits DOS drive and delete buttons in the directory-confined save/load view.
- [src/ui/save_load.cpp](../src/ui/save_load.cpp): permits cancelling host file dialogs with the right mouse button.
