// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/vga.hpp"
#include "game/top_scorers.hpp"

namespace bmh { struct GameState; }

namespace bmh::ui {

enum class TopScorersMetric : uint8_t { goals, assists, scorer };
enum class TopScorersScope : uint8_t { league, managers };
enum class TopScorersAction : uint8_t { open, set_metric, toggle_scope, close };

// Every visible result is bound to the active desk owner and controller scope.
// The two expected words are reserved as immutable request guards and must stay
// zero: menu 20 has no saved per-manager settings to edit.
struct TopScorersIdentity {
    int manager_slot = -1;
    int runtime_team = -1;
    int club_identity = -1;
    int league = -1;
    uint32_t day_counter = 0;
    uint64_t revision = 0;
    uint64_t scope = 0;
    uint64_t sequence = 0;
    uint16_t expected_settings_word = 0;
    uint16_t expected_family_word = 0;
    bool operator==(const TopScorersIdentity&) const = default;
};

struct TopScorersRequest {
    TopScorersIdentity identity;
    TopScorersAction action = TopScorersAction::open;
    TopScorersMetric metric = TopScorersMetric::goals;
    TopScorersScope scope = TopScorersScope::league;
    bool operator==(const TopScorersRequest&) const = default;
};

struct TopScorersState {
    TopScorersIdentity identity;
    TopScorersMetric metric = TopScorersMetric::goals;
    TopScorersScope scope = TopScorersScope::league;
    game::TopScorersProjection projection;
    Image underlay; // Captured accepted desktop for Source's cached-frame path.
    bool decorated_background = true;
    bool gradient_text = true;
    bool cached_backdrop = false;
};

game::TopScorersMetric top_scorers_metric_to_game(TopScorersMetric);
game::TopScorersScope top_scorers_scope_to_game(TopScorersScope);
TopScorersState top_scorers_state_from_game(const GameState&, const TopScorersIdentity&,
                                            TopScorersMetric, TopScorersScope,
                                            const Image& underlay = {});

} // namespace bmh::ui
