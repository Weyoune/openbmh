// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bmh { struct GameState; struct TurnResultSnapshot; class LeImage; }

namespace bmh::ui {

enum class MatchResultStatus { played, postponed, not_played, unknown };

struct MatchdayResultMatch {
    // Presentation strings are UTF-8; conversion to the original font happens
    // only in MatchdayResultsScreen.
    std::string home;
    std::string away;
    std::optional<int> home_goals;
    std::optional<int> away_goals;
    MatchResultStatus status = MatchResultStatus::unknown;
    // How a cup match was decided (cup result bits 10-11): 0 in regular time, 1 after
    // extra time, 2 on penalties - the shoot-out goals are then part of the score
    // (CupTie2 adds them), so a 1:1 won 5:3 on penalties is stored as 6:4.
    int decided = 0;
};

struct MatchdayResultGroup {
    std::string competition;
    bool cup = false;
    std::vector<MatchdayResultMatch> matches;
    // Source descriptor +0xcc is a league ID, or a cup-table entry address.
    // Cup IDs are the entry's u16 +0x18 (descriptor type minus one).
    std::optional<int> league_id;
    std::optional<int> cup_id;
    std::optional<int> cup_entry_id;
};

// Immutable UI copy: it owns no engine references or mutation hooks.
struct MatchdayResults {
    bool valid = false;
    uint32_t date = 0;
    std::string date_label;
    uint16_t calendar_entry = 0;
    uint16_t matchday = 0;
    std::vector<MatchdayResultGroup> groups;
};

using MatchdayTeamNameResolver = std::function<std::string(int runtime_team)>;
// Compatibility projection without edition text context: IDs and scores are
// resolved, but competition names remain empty. Production callers use the
// GameState overload to resolve owned UTF-8 titles from the effective edition.
MatchdayResults matchday_results_from_snapshot(const bmh::TurnResultSnapshot&,
                                                const MatchdayTeamNameResolver&,
                                                const bmh::LeImage&);
MatchdayResults matchday_results_from_snapshot(const bmh::TurnResultSnapshot&,
                                                const MatchdayTeamNameResolver&,
                                                const bmh::LeImage&,
                                                const bmh::GameState&);

} // namespace bmh::ui
