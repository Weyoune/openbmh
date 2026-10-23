// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The dimensions of a game world: team id ranges and the squad slot table.
//
// The original game (Classic) fixes them: 138 league clubs (90 German with 24 squad
// slots, 48 international with 16), 30 amateur clubs, 189 squadless foreign cup clubs,
// the pseudo team kNoTeam (0x165) for the national team / player pool, and the
// player pools behind the squads. Classic never changes the layout, so every shared
// function below returns exactly the original numbers there.
//
// The Modern season core (docs/new_engine) installs a layout derived from its world
// data, for example 168 league clubs with 24 slots each. One game per process, like
// the league tables of schedule.hpp.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bmh {

struct SquadRange;

struct WorldLayout {
    // Team id ranges, in this order: league clubs, amateur clubs (cup newcomers),
    // squadless foreign cup clubs, the pseudo team "no club", national teams.
    int league_teams = 138;      // ids 0 .. league_teams-1 have squads
    int german_teams = 90;       // the first league clubs (German leagues)
    int amateur_teams = 30;      // ids league_teams .. +amateur_teams-1, no squads
    int foreign_cup_teams = 189; // squadless foreign clubs (27 nations x 7)
    int national_teams = 62;     // after no_team

    // Squad slot table: first slot and slot count per league club, then the pools.
    std::vector<uint32_t> squad_start;   // per league club
    std::vector<uint8_t> squad_size;     // per league club
    uint32_t transfer_list = 0xB70;      // 44 slots: players on the transfer list
    uint32_t free_agents = 0xB9C;        // 80 slots: players without a club
    uint32_t foreign_offers = 0xBEC;     // 20 slots: players offered from abroad
    uint32_t pool_end = 0xC00;           // end of the pools = first slot of no_team
    uint32_t no_team_slots = 24;         // the national team / no-club squad
    uint32_t total_slots = 3150;         // including the reserve behind no_team

    int first_amateur() const { return league_teams; }
    int first_foreign_cup() const { return league_teams + amateur_teams; }
    int no_team() const { return first_foreign_cup() + foreign_cup_teams; }
    int total_teams() const { return no_team() + national_teams; }
    bool has_squad(int team) const { return (team >= 0 && team < league_teams) || team == no_team(); }
};

// The original dimensions (V2.24 and the supported German and English editions).
WorldLayout classic_world_layout();

// The layout of the running game (Classic unless a Modern session installed another).
const WorldLayout& world_layout();
void set_world_layout(WorldLayout layout);

// Shortcut for the pseudo team "no club" of the running game (Classic: kNoTeam, 0x165).
inline int no_team() { return world_layout().no_team(); }

}  // namespace bmh
