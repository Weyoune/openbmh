# Intentional differences from the original

Generated from source comment markers by `tools/public_release/collect_differences.py`.

See [Compatibility decisions](COMPATIBILITY.md) for the overall policy. These entries
describe deliberate changes; they are not a list of every known compatibility limit.

- [src/app/career_session.cpp](../src/app/career_session.cpp): supports designated set-piece record initialization and validation.
- [src/app/main.cpp](../src/app/main.cpp): display the repository-owned title at its native resolution until a complete left/right click, without loading balls.
- [src/app/quit_confirmation.hpp](../src/app/quit_confirmation.hpp): confirms application exit with a localized native dialog, with cancellation selected by default.
- [src/game/career.cpp](../src/game/career.cpp): skips reserved calendar positions when assigning a joining manager to a club.
- [src/game/career.hpp](../src/game/career.hpp): uses the supported player-generation ranges and excludes reserved calendar positions when assigning managers.
- [src/game/desk_actions.cpp](../src/game/desk_actions.cpp): uses the supported transfer-fee arithmetic and designated set-piece fields.
- [src/game/desk_actions.hpp](../src/game/desk_actions.hpp): supports designated set-piece records and the supported transfer-editor rules.
- [src/game/desk_actions.hpp](../src/game/desk_actions.hpp): a seller may accept an existing bid before expiry.
- [src/game/highscore.cpp](../src/game/highscore.cpp): reads the supported edition-specific high-score format from the loaded image.
- [src/game/lineup.hpp](../src/game/lineup.hpp): supports owner-bound designated set-piece selection.
- [src/game/match.hpp](../src/game/match.hpp): uses the enhanced injury selection and suspension rules.
- [src/game/match_engine.cpp](../src/game/match_engine.cpp): supports designated set-piece takers, the enhanced injury tables, and the extended suspension fields.
- [src/game/month.cpp](../src/game/month.cpp): keep the price product in 64 bits until division, retaining per-unit truncation and recipient policy.
- [src/game/national.cpp](../src/game/national.cpp): a new arrival must not inherit the previous occupant's retirement/refusal/month-processing flags.
- [src/game/season.cpp](../src/game/season.cpp): retains the enhanced academy player-generation strength bands with monthly youth budgets.
- [src/game/season.cpp](../src/game/season.cpp): age/retire each player once, even when a temporary offer row references his club slot or regeneration moves him to a later free-agent slot.
- [src/game/season.cpp](../src/game/season.cpp): includes transfer-market players in retirement so the five-bit age cannot wrap to 16.
- [src/game/season.cpp](../src/game/season.cpp): initializes the empty EM qualifier seeds with national teams, preventing club zero from entering the EM.
- [src/game/season.hpp](../src/game/season.hpp): retains the enhanced academy player-generation strength bands with monthly youth budgets.
- [src/game/stadium_blocks.cpp](../src/game/stadium_blocks.cpp): allow 144,000 standard places and size-aware construction times while retaining edition-owned unit prices.
- [src/game/state.hpp](../src/game/state.hpp): retains designated set-piece selections in the supported manager-record extension.
- [src/game/week.cpp](../src/game/week.cpp): widen products before annual-rate division so valid native account amounts do not overflow intermediate sums.
- [src/game/week.cpp](../src/game/week.cpp): an unknown history receives a grace month; later losses occur at most once per source month.
- [src/game/week.cpp](../src/game/week.cpp): uses the enhanced training rules: focus strength is at least five below its target, and completed skills rotate to another focus.
- [src/game/week.hpp](../src/game/week.hpp): uses the enhanced assistant-training rules, retaining the selected skill until it reaches its maximum.
- [src/game/weekly.cpp](../src/game/weekly.cpp): uses the supported weekly-event configuration, with its rule switch disabled.
- [src/game/weekly.hpp](../src/game/weekly.hpp): uses the supported weekly processing and transfer rule profile.
- [src/ui/bank.cpp](../src/ui/bank.cpp): read-only totals use wide arithmetic; individual saved account, price, holding and paid fields are unchanged.
- [src/ui/league_table.hpp](../src/ui/league_table.hpp): uses three points for a win in the supported rule profile.
- [src/ui/pitch.cpp](../src/ui/pitch.cpp): supports owner-bound designated set-piece selection in the extended manager record.
- [src/ui/save_load.cpp](../src/ui/save_load.cpp): focuses the host save filename on entry and selects an existing name for replacement.
- [src/ui/save_load.cpp](../src/ui/save_load.cpp): omits DOS drive and delete buttons in the directory-confined save/load view.
- [src/ui/save_load.cpp](../src/ui/save_load.cpp): permits cancelling host file dialogs with the right mouse button.
