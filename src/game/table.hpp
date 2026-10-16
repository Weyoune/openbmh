// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// League tables (BMH.exe V2.24).
//
// The original keeps no table: whenever one is needed (desk, table screen, season goals,
// position history) ComputeLeagueTable recomputes it from the result bytes of the season
// (block LeagueResults, see schedule.hpp) into a set of scratch arrays:
//   TableRuntimeField  goals for          TableRuntimeField2  goals against
//   TableRuntimeField3  points (signed)    TableRuntimeField4  wins     TableRuntimeField5  defeats
//   TableRuntimeField6  u16: bits 0-5 games, 6-10 previous rank, 11-15 rank (0 = first)
//   TableRuntimeField7  u32 form chain, 3 bits per game (newest lowest):
//                 venue << 2 | (0 defeat, 1 draw, 2 win) + 1; 4 = awarded/void
// all indexed by the club's draw position inside its league.
//
// AddResult3 adds one result, SortKey builds the sort key, AddResult2 sorts
// (descending, the quicksort Games) and stores the ranks.

#include <array>
#include <cstdint>

#include "game/state.hpp"

namespace bmh {

struct LeagueTable {
    int league = 0;
    int size = 0;
    std::array<uint8_t, 20> goals_for{}, goals_against{};
    std::array<int8_t, 20> points{};
    std::array<uint8_t, 20> wins{}, defeats{};
    std::array<uint16_t, 20> rank_word{};  // TableRuntimeField6
    std::array<uint32_t, 20> form{};

    int games(int club) const { return rank_word[club] & 0x3F; }
    int rank(int club) const { return rank_word[club] >> 11; }
    int previous_rank(int club) const { return rank_word[club] >> 6 & 0x1F; }
};

// The building blocks, also used for the cup group tables (AddResult):
// AddResult3 (flags 1 home side only, 2 away side only, 3 both), SortKey and
// AddResult2.
void add_result(LeagueTable& t, const int club[2], const uint8_t goals[2], int flags, int win_bonus);
uint32_t sort_key(const LeagueTable& t, int club, int parts);
void sort_table(LeagueTable& t, int parts);

// Matchdays of a league per season (LeagueMatchdayCounts = clubs * 2 - 2).
int league_matchdays(int league);

// ComputeLeagueTable(league, from, to, flags, points_mode): table over the matchdays
// from+1 .. to (clamped to the season). flags: 1 home games only, 2 away games only,
// 4 apply a human manager's point deduction (manager record +0x220). points_mode bit 0:
// three points for a win; bits 1-3 select the sort key parts (see sort_key).
// The ranks are recomputed after certain matchdays exactly as the original does, so the
// "previous rank" bits match its table screens.
LeagueTable compute_league_table(const GameState& g, int league, int from, int to, int flags, int points_mode);

}  // namespace bmh
