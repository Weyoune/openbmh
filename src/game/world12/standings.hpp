// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/fixtures.hpp"

namespace bmh::world12 {
enum class TableVenue:uint8_t { all,home,away };
struct TableQuery {
    int first_round=1,last_round=0; // inclusive; zero means the full season
    TableVenue venue=TableVenue::all;
    bool three_points=true;
    // As in the original table display: bits suppress points, goals for,
    // and goals against respectively. Zero is the normal league ranking.
    uint8_t suppressed_parts=0;
    std::map<ClubId,uint8_t> point_deductions;
};
struct StandingRow {
    ClubId club;
    uint8_t rank=0,previous_rank=0,games=0,wins=0,draws=0,defeats=0;
    uint8_t goals_for=0,goals_against=0,points=0;
    uint32_t form=0;
};
// Reuse the Classic points, sorting, form and previous-rank calculations in
// a bounded per-league workspace. Only the returned identities are global;
// no global club/player ID is packed into a legacy field.
std::vector<StandingRow> compute_standings(const World&,const SeasonResults&,
    LeagueId,int rounds_played,const TableQuery& query={});
}
