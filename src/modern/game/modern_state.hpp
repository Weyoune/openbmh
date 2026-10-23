// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The game state of the Modern season core.
//
// Modern started as a fork of the Classic season core (docs/new_engine). Its season
// functions take ModernState instead of GameState: the shared parts (formats, match
// core, line-ups, ratings) keep working on the GameState base, and overload resolution
// always prefers the Modern season function over a Classic one of the same name.
// Modern-only data (world layout, UTF-8 names, ...) is added here, never to GameState.

#include <cstddef>
#include <utility>

#include "game/state.hpp"
#include "game/world_layout.hpp"

namespace bmh::modern {

// Dimensions of the running world (WorldLayout). Modern code uses these accessors where
// the Classic sources have the fixed numbers (138 league clubs, 90 German clubs, 168, the
// pool slots 0xb70/0xb9c/0xbec/0xc00, 3150 slots); no_team() replaces kNoTeam (0x165).
inline int league_clubs() { return world_layout().league_teams; }
inline int german_clubs() { return world_layout().german_teams; }
inline int first_foreign_cup() { return world_layout().first_foreign_cup(); }
inline size_t transfer_list_slot() { return world_layout().transfer_list; }
inline size_t free_agent_slot() { return world_layout().free_agents; }
inline size_t foreign_offer_slot() { return world_layout().foreign_offers; }
inline size_t pool_end_slot() { return world_layout().pool_end; }
inline size_t total_squad_slots() { return world_layout().total_slots; }

// The callbacks the shared match core uses for season functions (SeasonHooks).
const SeasonHooks& season_hooks();

struct ModernState : GameState {
    ModernState() { season_hooks = &modern::season_hooks(); }
    // Takes over a state built by shared code (save loading, career setup).
    ModernState(GameState&& g) : GameState(std::move(g)) { season_hooks = &modern::season_hooks(); }
    ModernState(const GameState& g) : GameState(g) { season_hooks = &modern::season_hooks(); }
};

}  // namespace bmh::modern
