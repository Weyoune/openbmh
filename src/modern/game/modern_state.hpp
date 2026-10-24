// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The game state of the Modern season core.
//
// Modern started as a fork of the Classic season core (docs/new_engine). Its season
// functions take ModernState instead of GameState: the shared parts (formats, match
// core, line-ups, ratings) keep working on the GameState base, and overload resolution
// always prefers the Modern season function over a Classic one of the same name.
// Modern-only data (world layout, UTF-8 names, ...) is added here, never to GameState.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/state.hpp"
#include "game/world_layout.hpp"
#include "modern/game/almanac.hpp"
#include "modern/game/training_camps.hpp"

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

// The form history per manager (manager_results) holds 10 bytes per club with a record
// (Classic: 168 clubs, stride 0x690).
inline size_t manager_history_stride() { return size_t(first_foreign_cup()) * 10; }

struct ModernState : GameState {
    // --- Modern world (docs/new_engine, M3); empty when the game runs on Classic data -------
    // UTF-8 names and OBW identities by runtime index. The single-byte name pools of
    // GameState stay for the engine texts; interfaces show these.
    std::vector<std::string> world_club_id, world_club_name, world_club_short;   // per team
    std::vector<std::string> world_player_id, player_first, player_last;         // per player
    std::vector<std::string> player_nation;                                       // nation id per player
    std::vector<std::string> world_league_id, world_league_name;                 // per league index
    // Training camps (M4): Classic data gives the eight original resorts, a Modern world its catalogue.
    std::vector<CampMap> camp_maps;
    std::vector<TrainingCamp> training_camps;
    // The football almanac (docs/new_engine/almanach.md): every match of the career.
    Almanac almanac;
    // Toto with up to six columns (docs/new_engine/toto.md), per manager. Column 1 is the
    // original coupon (Coupon + manager * 0x14); columns 2-6 and the last settlement live here.
    struct PoolsColumns {
        std::vector<std::array<uint8_t, 11>> extra;  // columns 2-6, tips 1 home, 0 draw, 2 away
        std::vector<uint8_t> hits;                   // hits per column of the last settlement
        uint32_t paid = 0, won = 0;                  // stakes and fee, winnings of the last settlement
        bool operator==(const PoolsColumns&) const = default;
    };
    std::vector<PoolsColumns> pools;
    // Players whose age the data set gives (birth date); the others carry a placeholder the
    // career start replaces as in the original. Needed only while a career starts (not saved).
    std::vector<bool> world_age_known;

    ModernState() { season_hooks = &modern::season_hooks(); }
    // Takes over a state built by shared code (save loading, career setup).
    ModernState(GameState&& g) : GameState(std::move(g)) { season_hooks = &modern::season_hooks(); }
    ModernState(const GameState& g) : GameState(g) { season_hooks = &modern::season_hooks(); }
};

// A player who came from the data set (not a new person the engine made of his index): his
// values are the data set's (docs/new_engine/balance.md). Without a world: nobody.
inline bool keeps_world_values(const ModernState& g, int player) {
    return player >= 0 && size_t(player) < g.world_player_id.size() && !g.world_player_id[size_t(player)].empty() &&
           !g.world_player_id[size_t(player)].starts_with("player:gen-");
}

// A player of the data set whose age it gives (balance.md).
inline bool keeps_world_age(const ModernState& g, int player) {
    return keeps_world_values(g, player) && size_t(player) < g.world_age_known.size() && g.world_age_known[size_t(player)];
}

}  // namespace bmh::modern
