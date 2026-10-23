// SPDX-License-Identifier: GPL-3.0-only
#include "game/world_layout.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace bmh {

namespace {

WorldLayout& current() {
    static WorldLayout layout = classic_world_layout();
    return layout;
}

}  // namespace

WorldLayout classic_world_layout() {
    // Squad table of V2.24 (TeamsWithSquads, LargeSquadClubCount, NoTeamSquadStart): the
    // 90 German clubs have 24 slots, the 48 international clubs 16 from slot 90 * 24.
    WorldLayout w;
    w.squad_start.resize(size_t(w.league_teams));
    w.squad_size.resize(size_t(w.league_teams));
    for (int team = 0; team < w.league_teams; ++team) {
        const bool german = team < w.german_teams;
        w.squad_start[size_t(team)] = german ? uint32_t(team) * 24 : uint32_t(team) * 16 + uint32_t(w.german_teams) * 8;
        w.squad_size[size_t(team)] = german ? 24 : 16;
    }
    return w;
}

int WorldLayout::team_of_slot(int slot) const {
    if (slot < 0) return -1;
    if (uint32_t(slot) < transfer_list) {
        const auto it = std::upper_bound(squad_start.begin(), squad_start.end(), uint32_t(slot));
        return int(it - squad_start.begin()) - 1;
    }
    if (uint32_t(slot) >= pool_end && uint32_t(slot) < pool_end + no_team_slots) return no_team();
    return -1;
}

const WorldLayout& world_layout() { return current(); }

void set_world_layout(WorldLayout layout) {
    const size_t teams = size_t(layout.league_teams);
    if (layout.league_teams <= 0 || layout.german_teams < 0 || layout.german_teams > layout.league_teams ||
        layout.amateur_teams < 0 || layout.foreign_cup_teams < 0 || layout.national_teams < 0 ||
        layout.squad_start.size() != teams || layout.squad_size.size() != teams)
        throw std::invalid_argument("world layout: inconsistent team ranges");
    // Squads lie back to back in id order before the pools; the pools and the no-club
    // squad follow in their fixed order.
    uint32_t next = 0;
    for (size_t team = 0; team < teams; ++team) {
        if (layout.squad_start[team] != next || layout.squad_size[team] == 0)
            throw std::invalid_argument("world layout: squads must be consecutive");
        next += layout.squad_size[team];
    }
    if (layout.transfer_list != next || layout.free_agents != layout.transfer_list + 44 ||
        layout.foreign_offers != layout.free_agents + 80 || layout.pool_end != layout.foreign_offers + 20 ||
        layout.total_slots < layout.pool_end + layout.no_team_slots)
        throw std::invalid_argument("world layout: pools must follow the squads");
    // Squad slot values hold 12-bit player ids; loans store 10-bit club ids.
    if (layout.total_teams() > 1023) throw std::invalid_argument("world layout: too many teams");
    current() = std::move(layout);
}

}  // namespace bmh
