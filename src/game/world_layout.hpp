// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The dimensions of a game world: team id ranges and the squad slot table.
//
// The original game fixes them (and so does every supported edition, German and English):
//   ids   0..137  the 138 league clubs: 90 clubs of the five domestic leagues with 24 squad slots,
//                 48 clubs of the four international leagues with 16
//   ids 138..167  30 amateur clubs (cup newcomers), without squads
//   ids 168..356  189 foreign cup clubs without squads (a strength byte each)
//   id  357       the pseudo team "no club" (kNoTeam, 0x165): national team / player pool, 24 slots
//   ids 358..418  61 national teams
// and behind the squads the three player pools (transfer list, free agents, offers from abroad).
//
// Until 1.0.12 these numbers stood as literals all over the engine (138, 168, 0x165, 90 * 24, ...).
// WorldLayout names them in one place. OpenBMH Classic never installs another layout:
// classic_world_layout() returns exactly the original numbers, so every function that asks the
// layout computes what it computed before. A later edition with more leagues (OpenBMH Extended)
// installs its own layout instead of patching literals.
//
// One game per process, like the league tables of schedule.hpp.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bmh {

struct WorldLayout {
    // Team id ranges, in this order: league clubs, amateur clubs, squadless foreign cup clubs,
    // the pseudo team "no club", national teams.
    int league_teams = 138;       // ids 0 .. league_teams-1 have squads
    int domestic_teams = 90;      // the first league clubs (the five domestic leagues)
    int amateur_teams = 30;       // ids league_teams .. +amateur_teams-1, no squads
    int foreign_cup_teams = 189;  // squadless foreign clubs (27 nations x 7)
    int national_teams = 62;      // "no club" (the first of them) and the 61 national teams

    // Squad slot table: first slot and slot count per league club, then the pools.
    std::vector<uint32_t> squad_start;   // per league club
    std::vector<uint8_t> squad_size;     // per league club
    uint32_t transfer_list = 0xB70;      // 44 slots: players on the transfer list
    uint32_t free_agents = 0xB9C;        // 80 slots: players without a club
    uint32_t foreign_offers = 0xBEC;     // 20 slots: players offered from abroad
    uint32_t pool_end = 0xC00;           // end of the pools = first slot of "no club"
    uint32_t no_team_slots = 24;         // the squad of "no club"
    uint32_t total_slots = 3150;         // including the reserve behind "no club"

    int first_amateur() const { return league_teams; }
    // The first team without a squad of its own that is a foreign cup club (Classic: 168); the
    // clubs before it are "the clubs with league records" (league clubs + amateurs).
    int first_foreign_cup() const { return league_teams + amateur_teams; }
    int no_team() const { return first_foreign_cup() + foreign_cup_teams; }
    int total_teams() const { return no_team() + national_teams; }
    bool has_squad(int team) const { return (team >= 0 && team < league_teams) || team == no_team(); }
};

// The original dimensions (V2.24 and the supported German and English editions).
WorldLayout classic_world_layout();

// The layout of the running game (Classic unless another one was installed).
const WorldLayout& world_layout();

// Installs a layout; throws std::invalid_argument when its ranges or its slot table are inconsistent.
void set_world_layout(WorldLayout layout);

}  // namespace bmh
