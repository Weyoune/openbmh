// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/state.hpp"

namespace bmh::game {

enum class TopScorersMetric : uint8_t { goals, assists, scorer };
enum class TopScorersScope : uint8_t { league, managers };

struct TopScorersRow {
    int rank = 0;
    int player = -1;
    int team = -1;
    int squad_slot = -1;
    // Values written to Original's scratch row: unused individual counters
    // are zero unless their corresponding flag is selected.
    uint8_t goals = 0;
    uint8_t assists = 0;
    uint8_t score = 0;
    std::string player_name;
    std::string team_name;
    bool operator==(const TopScorersRow&) const = default;
};

struct TopScorersProjection {
    int manager_slot = -1;
    int league = -1;
    TopScorersMetric metric = TopScorersMetric::goals;
    TopScorersScope scope = TopScorersScope::league;
    std::vector<TopScorersRow> rows;
    bool no_results = true;
    bool operator==(const TopScorersProjection&) const = default;
};

// Read-only port of Source TopScorersFromGame. It traverses the selected domestic
// league in runtime team order and each club in squad-slot order, inserting
// strictly ahead of lower scores (equal scores retain traversal order).
// It writes neither GameState nor the Source scratch result table and draws no RNG.
TopScorersProjection top_scorers_from_game(const GameState& game, int manager_slot,
                                            TopScorersMetric metric,
                                            TopScorersScope scope);

} // namespace bmh::game
