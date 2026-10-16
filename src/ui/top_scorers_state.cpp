// SPDX-License-Identifier: GPL-3.0-only
#include "ui/top_scorers_state.hpp"

#include <stdexcept>

#include "game/state.hpp"

namespace bmh::ui {

game::TopScorersMetric top_scorers_metric_to_game(TopScorersMetric metric) {
    switch (metric) {
    case TopScorersMetric::goals: return game::TopScorersMetric::goals;
    case TopScorersMetric::assists: return game::TopScorersMetric::assists;
    case TopScorersMetric::scorer: return game::TopScorersMetric::scorer;
    }
    throw std::invalid_argument("top scorers: unknown metric");
}

game::TopScorersScope top_scorers_scope_to_game(TopScorersScope scope) {
    switch (scope) {
    case TopScorersScope::league: return game::TopScorersScope::league;
    case TopScorersScope::managers: return game::TopScorersScope::managers;
    }
    throw std::invalid_argument("top scorers: unknown scope");
}

TopScorersState top_scorers_state_from_game(const GameState& source,
                                            const TopScorersIdentity& identity,
                                            TopScorersMetric metric,
                                            TopScorersScope scope,
                                            const Image& underlay) {
    TopScorersState state;
    state.identity = identity;
    state.metric = metric;
    state.scope = scope;
    state.underlay = underlay;
    state.projection = game::top_scorers_from_game(
        source, identity.manager_slot, top_scorers_metric_to_game(metric), top_scorers_scope_to_game(scope));
    if (state.projection.league != identity.league)
        throw std::invalid_argument("top scorers: identity league does not match projection");
    return state;
}

} // namespace bmh::ui
